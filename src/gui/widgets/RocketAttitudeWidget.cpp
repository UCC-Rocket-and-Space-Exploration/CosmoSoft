#include "gui/widgets/RocketAttitudeWidget.h"
#include "gui/AttitudeMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <memory>
#include <numbers>
#include <vector>

namespace {
struct ModelFace {
  std::array<QVector3D, 4> vertices;
  QVector3D normal;
};

// The established rocket profile and fins are model geometry, independent of
// attitude, theme and viewport. Build them once rather than on every repaint.
const std::vector<ModelFace> &rocketMesh() {
  static const auto mesh = [] {
    std::vector<ModelFace> result;
    result.reserve(292);
    const auto add = [&](std::array<QVector3D, 4> vertices) {
      result.push_back(
          {vertices, QVector3D::crossProduct(vertices[1] - vertices[0],
                                             vertices[2] - vertices[0])
                         .normalized()});
    };
    const std::array<QPointF, 10> profile{{{0, -0.76},
                                           {0.09, -0.76},
                                           {0.12, -0.65},
                                           {0.16, -0.60},
                                           {0.16, 0.40},
                                           {0.155, 0.55},
                                           {0.13, 0.68},
                                           {0.10, 0.80},
                                           {0.055, 0.92},
                                           {0, 1.0}}};
    const auto vertex = [](const QPointF &p, double angle) {
      return QVector3D(static_cast<float>(p.x() * std::cos(angle)),
                       static_cast<float>(p.y()),
                       static_cast<float>(p.x() * std::sin(angle)));
    };
    for (int segment = 0; segment < 32; ++segment) {
      const double a = segment * 2.0 * std::numbers::pi / 32.0;
      const double b = (segment + 1) * 2.0 * std::numbers::pi / 32.0;
      for (std::size_t i = 1; i < profile.size(); ++i)
        add({vertex(profile[i - 1], a), vertex(profile[i], a),
             vertex(profile[i], b), vertex(profile[i - 1], b)});
    }
    for (int i = 0; i < 4; ++i) {
      const double angle = i * std::numbers::pi / 2.0;
      const QVector3D radial(static_cast<float>(std::cos(angle)), 0,
                             static_cast<float>(std::sin(angle)));
      const auto fin = [&](float r, float y) {
        return radial * r + QVector3D(0, y, 0);
      };
      add({fin(.14F, -.10F), fin(.42F, -.55F), fin(.42F, -.70F),
           fin(.14F, -.60F)});
    }
    return result;
  }();
  return mesh;
}
} // namespace

/** @brief Native 3D attitude instrument with a cached, depth-layered scene. */
class RocketAttitudeCanvas : public QWidget {
public:
  /** @brief Create a scalable instrument with a fixed oblique camera. */
  explicit RocketAttitudeCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("attitudeCanvas");
    setMinimumSize(100, 96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Rocket orientation against fixed X, Y and Z axes"));
  }
  /** @brief Replace the body-to-world rotation, or clear unavailable attitude.
   */
  void setOrientation(std::optional<QQuaternion> orientation) {
    if (m_orientation == orientation)
      return;
    m_orientation = orientation;
    invalidateScene();
  }

  /** @brief Mark the held live pose without changing its supplied rotation. */
  void setStale(bool stale) {
    if (m_stale == stale)
      return;
    m_stale = stale;
    invalidateScene();
  }

  /** @brief Invalidate theme-dependent painting after a palette change. */
  void invalidateScene() {
    m_scene = {};
    update();
  }

protected:
  /** @brief Project and depth-sort a small rocket mesh using native Qt
   * painting. */
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const auto pixelSize = (QSizeF(size()) * devicePixelRatioF()).toSize();
    if (m_scene.size() != pixelSize ||
        m_scene.devicePixelRatioF() != devicePixelRatioF()) {
      m_scene = QPixmap(pixelSize);
      m_scene.setDevicePixelRatio(devicePixelRatioF());
      m_scene.fill(Qt::transparent);
      QPainter scenePainter(&m_scene);
      drawScene(scenePainter);
    }
    painter.drawPixmap(0, 0, m_scene);
  }

  /** @brief Rebuild the scene at the new viewport size. */
  void resizeEvent(QResizeEvent *event) override {
    m_scene = {};
    QWidget::resizeEvent(event);
  }

private:
  void drawScene(QPainter &painter) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(Theme::kBgPanel()));
    const float scale =
        static_cast<float>(std::min(width() - 22, height() - 12)) / 2.65F;
    const QPointF center(width() / 2.0, height() / 2.0);
    const QQuaternion camera = QQuaternion::fromEulerAngles(22, -32, 0);
    const auto project = [&](const QVector3D &point) {
      const auto view = camera.rotatedVector(point);
      return center + QPointF(view.x() * scale, -view.y() * scale);
    };

    const double radius = scale * 1.16;
    QRadialGradient well(center, radius);
    well.setColorAt(0, QColor(Theme::kBgInput()));
    well.setColorAt(1, QColor(Theme::kBgPanel()));
    painter.setPen(Qt::NoPen);
    painter.setBrush(well);
    painter.drawEllipse(center, radius, radius);

    const auto ringPoint = [](int plane, double angle, float r = 1.10F) {
      const float a = r * static_cast<float>(std::cos(angle));
      const float b = r * static_cast<float>(std::sin(angle));
      return plane == 0   ? QVector3D(a, 0, b)
             : plane == 1 ? QVector3D(a, b, 0)
                          : QVector3D(0, a, b);
    };
    const auto drawCage = [&](bool front) {
      for (int plane = 0; plane < 3; ++plane) {
        QPainterPath arc;
        bool connected = false;
        for (int i = 0; i < 96; ++i) {
          const double a = i * 2.0 * std::numbers::pi / 96.0;
          const double b = (i + 1) * 2.0 * std::numbers::pi / 96.0;
          const auto start = ringPoint(plane, a), end = ringPoint(plane, b);
          if ((camera.rotatedVector((start + end) * .5F).z() >= 0) != front) {
            connected = false;
            continue;
          }
          if (!connected)
            arc.moveTo(project(start));
          arc.lineTo(project(end));
          connected = true;
        }
        QColor line(plane == 0 ? Theme::kBorderDefault()
                               : Theme::kBorderLight());
        line.setAlpha(front ? 145 : 70);
        painter.setPen(
            QPen(line, front ? 1.1 : .8, front ? Qt::SolidLine : Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arc);
        for (int tick = 0; tick < 24; ++tick) {
          if (plane != 0 && tick % 2)
            continue;
          const double angle = tick * std::numbers::pi / 12.0;
          const auto start = ringPoint(plane, angle);
          if ((camera.rotatedVector(start).z() >= 0) != front)
            continue;
          const auto end =
              ringPoint(plane, angle, tick % 6 == 0 ? 1.19F : 1.14F);
          painter.setPen(QPen(line, 1));
          painter.drawLine(project(start), project(end));
        }
      }
    };
    drawCage(false);
    const std::array<QVector3D, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<QColor, 3> colors{{QColor(Theme::kWarning()),
                                        QColor(Theme::kSuccess()),
                                        QColor(Theme::kInfo())}};
    for (int i = 0; i < 3; ++i) {
      painter.setPen(QPen(colors[i], 1.5));
      painter.drawLine(project(axes[i] * 1.13F), project(axes[i] * 1.30F));
      const auto end = project(axes[i] * 1.30F);
      painter.setBrush(colors[i]);
      painter.drawEllipse(end, 1.7, 1.7);
      QColor negative(Theme::kTextDim());
      negative.setAlpha(100);
      painter.setPen(QPen(negative, 1));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(project(-axes[i] * 1.14F), 1.7, 1.7);
      QFont axisFont(Theme::kFontMono);
      axisFont.setPixelSize(9);
      axisFont.setBold(true);
      painter.setFont(axisFont);
      painter.setPen(QColor(Theme::kTextPrimary()));
      auto label = project(axes[i] * 1.48F);
      label.setX(std::clamp(label.x(), 7.0, width() - 7.0));
      label.setY(std::clamp(label.y(), 6.0, height() - 6.0));
      painter.drawText(QRectF(label.x() - 6, label.y() - 6, 12, 12),
                       Qt::AlignCenter, QString(QChar('X' + i)));
    }
    if (!m_orientation) {
      drawCage(true);
      return;
    }

    struct Face {
      QPolygonF polygon;
      float depth = 0;
      QColor color;
    };
    std::vector<Face> faces;
    faces.reserve(rocketMesh().size());
    const QVector3D lightDirection = QVector3D(-.4F, .7F, 1).normalized();
    const auto viewDirection = camera.conjugated().rotatedVector({0, 0, 1});
    const auto highlightDirection =
        (lightDirection + viewDirection).normalized();
    const QColor orange =
        QColor(Theme::kWarning())
            .lighter(QColor(Theme::kBgBase()).lightness() > 127 ? 150 : 100);
    for (const auto &modelFace : rocketMesh()) {
      const auto normal = m_orientation->rotatedVector(modelFace.normal);
      const float diffuse =
          std::max(0.0F, QVector3D::dotProduct(normal, lightDirection));
      const float highlight = std::pow(
          std::max(0.0F, QVector3D::dotProduct(normal, highlightDirection)),
          24);
      const float lighting = .55F + .38F * diffuse + .18F * highlight;
      Face face{{}, 0, orange.darker(static_cast<int>(100 / lighting))};
      for (const auto &vertex : modelFace.vertices) {
        const auto world = m_orientation->rotatedVector(vertex);
        face.polygon << project(world);
        face.depth += camera.rotatedVector(world).z();
      }
      face.depth /= 4;
      faces.push_back(std::move(face));
    }
    std::stable_sort(
        faces.begin(), faces.end(),
        [](const Face &a, const Face &b) { return a.depth < b.depth; });
    painter.setPen(Qt::NoPen);
    painter.setOpacity(m_stale ? .55 : 1);
    for (const auto &face : faces) {
      painter.setBrush(face.color);
      painter.drawPolygon(face.polygon);
    }
    painter.setOpacity(1);
    drawCage(true);

    // A nose marker identifies pointing direction even when looking down the
    // body.
    const auto nose = m_orientation->rotatedVector({0, 1, 0});
    const auto tip = project(nose * 1.18F);
    painter.setPen(QPen(orange, 1.2));
    painter.setBrush(QColor(Theme::kBgPanel()));
    painter.drawLine(project(nose), tip);
    QPolygonF marker;
    marker << tip + QPointF(0, -3) << tip + QPointF(3, 0) << tip + QPointF(0, 3)
           << tip + QPointF(-3, 0);
    painter.drawPolygon(marker);
  }

private:
  std::optional<QQuaternion> m_orientation;
  QPixmap m_scene;
  bool m_stale = false;
};

RocketAttitudeWidget::RocketAttitudeWidget(QWidget *parent) : QFrame(parent) {
  setObjectName("rocketAttitudeWidget");
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setMinimumWidth(240);
  setFixedHeight(204);
  auto layout = std::make_unique<QVBoxLayout>();
  layout->setContentsMargins(12, 8, 12, 8);
  layout->setSpacing(4);
  const auto addLabel = [&](const QString &text, const char *name) {
    auto label = std::make_unique<QLabel>(text, this);
    label->setObjectName(name);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                   Qt::TextSelectableByKeyboard);
    return label.release();
  };
  auto header = std::make_unique<QHBoxLayout>();
  header->addWidget(addLabel(tr("ROCKET ATTITUDE"), "attitudeTitle"));
  header->addStretch();
  m_status = addLabel({}, "attitudeStatus");
  header->addWidget(m_status);
  layout->addLayout(header.release());
  auto canvas = std::make_unique<RocketAttitudeCanvas>(this);
  m_canvas = canvas.get();
  m_canvas->setToolTip(tr("Fixed world axes: X east · Y up · Z south"));
  layout->addWidget(canvas.release(), 1);
  m_angles = addLabel({}, "attitudeAngles");
  m_angles->setFixedHeight(34);
  m_angles->setToolTip(tr("Recorded FC X/Y/Z angles in degrees"));
  layout->addWidget(m_angles);
  setLayout(layout.release());
  auto timer = std::make_unique<QTimer>(this);
  m_staleTimer = timer.get();
  timer->setSingleShot(true);
  timer->setTimerType(Qt::PreciseTimer);
  connect(timer.release(), &QTimer::timeout, this,
          &RocketAttitudeWidget::refreshStatus);
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &RocketAttitudeWidget::refreshStyleSheet);
  resetDisplay();
  refreshStyleSheet();
}

void RocketAttitudeWidget::setSample(const FlightSample &sample, bool replay) {
  m_replay = replay;
  const auto orientation =
      cosmo::gui::attitudeQuaternion(sample.angularRotation);
  m_recordedAngles = sample.angularRotation;
  m_available = orientation.has_value();
  m_canvas->setOrientation(orientation);
  m_staleTimer->stop();
  if (m_available) {
    m_lastUpdate.start();
  }
  refreshAngles();
  refreshStatus();
}

void RocketAttitudeWidget::refreshAngles() {
  const QLocale locale;
  const auto value = [&](double angle) {
    return m_available ? locale.toString(std::remainder(angle, 360.0), 'f', 1)
                       : tr("—");
  };
  m_angles->setText(
      QStringLiteral(
          "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\">"
          "<tr style=\"color:%1;font-size:9px\"><td>X °</td><td "
          "align=\"center\">Y °</td>"
          "<td align=\"right\">Z °</td></tr>"
          "<tr style=\"font-weight:600\"><td>%2</td><td "
          "align=\"center\">%3</td>"
          "<td align=\"right\">%4</td></tr></table>")
          .arg(Theme::kTextMid(), value(m_recordedAngles.x),
               value(m_recordedAngles.y), value(m_recordedAngles.z)));
  m_canvas->setAccessibleDescription(
      m_available
          ? tr("Recorded FC angles: X %1 degrees, Y %2 degrees, Z %3 degrees")
                .arg(value(m_recordedAngles.x), value(m_recordedAngles.y),
                     value(m_recordedAngles.z))
          : tr("No supplied orientation"));
}

void RocketAttitudeWidget::resetDisplay() {
  m_available = false;
  m_staleTimer->stop();
  m_canvas->setOrientation(std::nullopt);
  refreshAngles();
  refreshStatus();
}

void RocketAttitudeWidget::refreshStatus() {
  const qint64 elapsed = m_lastUpdate.isValid() ? m_lastUpdate.elapsed() : 0;
  // Reschedule if the event arrives early rather than leaving Live
  // indefinitely.
  if (m_available && !m_replay && elapsed < 2000)
    m_staleTimer->start(static_cast<int>(2000 - elapsed));
  m_status->setText(!m_available      ? QString()
                    : m_replay        ? tr("Replay")
                    : elapsed >= 2000 ? tr("Stale")
                                      : tr("Live"));
  m_status->setToolTip(
      m_available && !m_replay && elapsed >= 2000
          ? tr("No new samples; showing the last received pose")
          : QString());
  const bool stale = m_available && !m_replay && elapsed >= 2000;
  m_canvas->setStale(stale);
  const auto state = !m_available ? QString()
                     : stale      ? QStringLiteral("stale")
                     : m_replay   ? QStringLiteral("replay")
                                  : QStringLiteral("live");
  if (m_status->property("poseState").toString() != state) {
    m_status->setProperty("poseState", state);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_status->update();
  }
}

void RocketAttitudeWidget::refreshStyleSheet() {
  setStyleSheet(
      QStringLiteral(
          "QFrame#rocketAttitudeWidget { background: %1; border: 1px solid %4; "
          "border-radius: %5px; }"
          "QFrame#rocketAttitudeWidget QLabel { color: %2; "
          "background: transparent; border: none; font-family: %6; "
          "font-size: %7px; }"
          "QFrame#rocketAttitudeWidget QLabel#attitudeStatus { color: %3; "
          "font-size:9px; }"
          "QFrame#rocketAttitudeWidget QLabel#attitudeTitle { font-size: 10px; "
          "font-weight:700; }"
          "QFrame#rocketAttitudeWidget QLabel#attitudeAngles { border-top:1px "
          "solid %4; padding-top:4px; }")
          .arg(Theme::kBgPanel(), Theme::kTextPrimary(), Theme::kTextMid(),
               Theme::kBorderSubtle())
          .arg(Theme::kRadiusMd)
          .arg(Theme::kFontMono)
          .arg(Theme::kFontSizeSm));
  m_status->setStyleSheet(
      QStringLiteral("QLabel[poseState=\"stale\"] { color:%1; }"
                     "QLabel[poseState=\"live\"] { color:%2; }")
          .arg(Theme::kWarning(), Theme::kSuccess()));
  m_angles->setStyleSheet(
      QStringLiteral("font-size:%1px;").arg(m_compact ? 10 : 11));
  refreshAngles();
  m_canvas->invalidateScene();
}

void RocketAttitudeWidget::setCompact(bool compact) {
  m_compact = compact;
  setMinimumWidth(compact ? 136 : 240);
  m_canvas->setMinimumWidth(compact ? 100 : 140);
  layout()->setContentsMargins(compact ? 8 : 12, 8, compact ? 8 : 12, 8);
  findChild<QLabel *>("attitudeTitle")
      ->setText(compact ? tr("ATTITUDE") : tr("ROCKET ATTITUDE"));
  m_status->setVisible(true);
  refreshStyleSheet();
}
