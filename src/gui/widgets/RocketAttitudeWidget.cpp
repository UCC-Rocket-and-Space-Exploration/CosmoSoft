#include "gui/widgets/RocketAttitudeWidget.h"
#include "gui/AttitudeMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <memory>
#include <numbers>
#include <vector>

/** @brief Small software-rendered 3D instrument; paints only when Qt requests
 * it. */
class RocketAttitudeCanvas : public QWidget {
public:
  /** @brief Create a scalable instrument with a fixed oblique camera. */
  explicit RocketAttitudeCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(140, 112);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Rocket orientation against fixed X, Y and Z axes"));
  }
  /** @brief Replace the body-to-world rotation, or clear unavailable attitude.
   */
  void setOrientation(std::optional<QQuaternion> orientation) {
    m_orientation = orientation;
    update();
  }

protected:
  /** @brief Project and depth-sort a small rocket mesh using native Qt
   * painting. */
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(Theme::kBgPanel()));
    const float scale = static_cast<float>(std::min(width(), height())) / 3.6F;
    const QPointF center(width() / 2.0, height() / 2.0);
    const QQuaternion camera = QQuaternion::fromEulerAngles(18, -32, 0);
    const auto project = [&](const QVector3D &point) {
      const auto view = camera.rotatedVector(point);
      return center + QPointF(view.x() * scale, -view.y() * scale);
    };

    painter.setPen(QPen(QColor(Theme::kBorderLight()), 1));
    for (int plane = 0; plane < 3; ++plane) {
      QPolygonF ring;
      for (int i = 0; i <= 64; ++i) {
        const double angle = i * 2.0 * std::numbers::pi / 64.0;
        const float a = static_cast<float>(1.4 * std::cos(angle));
        const float b = static_cast<float>(1.4 * std::sin(angle));
        ring << project(plane == 0   ? QVector3D(a, 0, b)
                        : plane == 1 ? QVector3D(a, b, 0)
                                     : QVector3D(0, a, b));
      }
      painter.drawPolyline(ring);
    }
    const std::array<QVector3D, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<QColor, 3> colors{{QColor(Theme::kWarning()),
                                        QColor(Theme::kSuccess()),
                                        QColor(Theme::kInfo())}};
    for (int i = 0; i < 3; ++i) {
      painter.setPen(QPen(colors[i], 1.5));
      painter.drawLine(project(-axes[i] * 1.45F), project(axes[i] * 1.45F));
      painter.setPen(QColor(Theme::kTextPrimary()));
      painter.drawText(project(axes[i] * 1.65F) + QPointF(-4, 4),
                       QString(QChar('X' + i)));
    }
    if (!m_orientation) {
      return;
    }

    struct Face {
      QPolygonF polygon;
      float depth = 0;
      QColor color;
    };
    std::vector<Face> faces;
    const auto addFace = [&](std::initializer_list<QVector3D> vertices,
                             QColor color) {
      Face face{{}, 0, color};
      if (vertices.size() >= 3) {
        const auto *v = vertices.begin();
        const auto normal = m_orientation->rotatedVector(
            QVector3D::crossProduct(v[1] - v[0], v[2] - v[0]).normalized());
        const float light =
            0.65F +
            0.35F *
                std::max(0.0F, QVector3D::dotProduct(
                                   normal,
                                   QVector3D(-0.4F, 0.7F, 1.0F).normalized()));
        face.color = color.darker(static_cast<int>(100.0F / light));
      }
      for (const auto &vertex : vertices) {
        const auto world = m_orientation->rotatedVector(vertex);
        face.polygon << project(world);
        face.depth += camera.rotatedVector(world).z();
      }
      face.depth /= static_cast<float>(vertices.size());
      faces.push_back(std::move(face));
    };
    // Continuous rounded nose and a tapered tail, with swept fins.
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
    const QColor orange =
        QColor(Theme::kWarning())
            .lighter(QColor(Theme::kBgBase()).lightness() > 127 ? 150 : 100);
    for (int segment = 0; segment < 32; ++segment) {
      const double a = segment * 2.0 * std::numbers::pi / 32.0;
      const double b = (segment + 1) * 2.0 * std::numbers::pi / 32.0;
      const auto vertex = [](const QPointF &p, double angle) {
        return QVector3D(static_cast<float>(p.x() * std::cos(angle)),
                         static_cast<float>(p.y()),
                         static_cast<float>(p.x() * std::sin(angle)));
      };
      for (std::size_t i = 1; i < profile.size(); ++i) {
        addFace({vertex(profile[i - 1], a), vertex(profile[i], a),
                 vertex(profile[i], b), vertex(profile[i - 1], b)},
                orange);
      }
    }
    for (int i = 0; i < 4; ++i) {
      const double a = i * std::numbers::pi / 2.0;
      const QVector3D radial(static_cast<float>(std::cos(a)), 0,
                             static_cast<float>(std::sin(a)));
      const auto finPoint = [&](float radius, float y) {
        return radial * radius + QVector3D(0, y, 0);
      };
      addFace({finPoint(0.14F, -0.10F), finPoint(0.42F, -0.55F),
               finPoint(0.42F, -0.70F), finPoint(0.14F, -0.60F)},
              orange);
    }
    std::stable_sort(
        faces.begin(), faces.end(),
        [](const Face &a, const Face &b) { return a.depth < b.depth; });
    painter.setPen(Qt::NoPen);
    for (const auto &face : faces) {
      painter.setBrush(face.color);
      painter.drawPolygon(face.polygon);
    }
  }

private:
  std::optional<QQuaternion> m_orientation;
};

RocketAttitudeWidget::RocketAttitudeWidget(QWidget *parent) : QFrame(parent) {
  setObjectName("rocketAttitudeWidget");
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  setMinimumWidth(240);
  setFixedHeight(204);
  auto layout = std::make_unique<QVBoxLayout>();
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(6);
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
  m_angles->setToolTip(tr("Recorded FC angles in degrees"));
  m_available = orientation.has_value();
  m_canvas->setOrientation(orientation);
  m_staleTimer->stop();
  if (m_available) {
    const QLocale locale;
    m_angles->setText(
        tr("<table width=\"100%\" "
           "cellspacing=\"0\"><tr><td>X</td><td>Y</td><td>Z</td></tr>"
           "<tr><td>%1°</td><td>%2°</td><td>%3°</td></tr></table>")
            .arg(locale.toString(
                     std::remainder(sample.angularRotation.x, 360.0), 'f', 1),
                 locale.toString(
                     std::remainder(sample.angularRotation.y, 360.0), 'f', 1),
                 locale.toString(
                     std::remainder(sample.angularRotation.z, 360.0), 'f', 1)));
    m_lastUpdate.start();
  } else {
    m_angles->setText(
        tr("<table width=\"100%\" "
           "cellspacing=\"0\"><tr><td>X</td><td>Y</td><td>Z</td></tr>"
           "<tr><td>—</td><td>—</td><td>—</td></tr></table>"));
  }
  m_canvas->setAccessibleDescription(
      QTextDocumentFragment::fromHtml(m_angles->text()).toPlainText());
  refreshStatus();
}

void RocketAttitudeWidget::resetDisplay() {
  m_available = false;
  m_staleTimer->stop();
  m_canvas->setOrientation(std::nullopt);
  m_angles->setText(
      tr("<table width=\"100%\" "
         "cellspacing=\"0\"><tr><td>X</td><td>Y</td><td>Z</td></tr>"
         "<tr><td>—</td><td>—</td><td>—</td></tr></table>"));
  m_canvas->setAccessibleDescription(tr("Attitude unavailable"));
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
}

void RocketAttitudeWidget::refreshStyleSheet() {
  setStyleSheet(
      QStringLiteral(
          "QFrame#rocketAttitudeWidget { background: %1; border: 1px solid %4; "
          "border-radius: %5px; }"
          "QFrame#rocketAttitudeWidget QLabel { color: %2; "
          "background: transparent; border: none; font-family: %6; "
          "font-size: %7px; }"
          "QFrame#rocketAttitudeWidget QLabel#attitudeStatus { color: %3; }"
          "QFrame#rocketAttitudeWidget QLabel#attitudeTitle { font-size: 13px; "
          "font-weight: 700; "
          "letter-spacing: 1px; }")
          .arg(Theme::kBgPanel(), Theme::kTextPrimary(), Theme::kTextMid(),
               Theme::kBorderSubtle())
          .arg(Theme::kRadiusMd)
          .arg(Theme::kFontMono)
          .arg(Theme::kFontSizeSm));
  m_canvas->update();
}
