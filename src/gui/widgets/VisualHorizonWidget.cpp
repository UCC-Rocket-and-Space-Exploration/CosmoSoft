#include "gui/widgets/VisualHorizonWidget.h"
#include "gui/AttitudeMath.h"
#include "gui/NavballMath.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <numbers>

VisualHorizonWidget::VisualHorizonWidget(QWidget *parent) : QFrame(parent) {
  setObjectName("visualHorizonWidget");
  setAccessibleName(tr("Visual horizon"));
  setMinimumWidth(136);
  setFixedHeight(204);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setToolTip(tr("View along the rocket nose · blue sky / brown ground · "
                "supplied FC orientation"));
  auto timer = std::make_unique<QTimer>(this);
  m_staleTimer = timer.get();
  timer->setSingleShot(true);
  timer->setTimerType(Qt::PreciseTimer);
  connect(timer.release(), &QTimer::timeout, this, [this] {
    if (m_arrival.isValid() && m_arrival.elapsed() < 2000)
      m_staleTimer->start(static_cast<int>(2000 - m_arrival.elapsed()));
    update();
  });
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &VisualHorizonWidget::refreshTheme);
  refreshTheme();
  resetDisplay();
}
void VisualHorizonWidget::refreshTheme() {
  m_ball = {};
  update();
}
void VisualHorizonWidget::resetDisplay() {
  m_orientation.reset();
  m_ball = {};
  m_staleTimer->stop();
  setAccessibleDescription(tr("No supplied orientation"));
  update();
}
void VisualHorizonWidget::setSample(const FlightSample &sample, bool replay) {
  m_replay = replay;
  const auto pose = cosmo::gui::attitudeQuaternion(sample.angularRotation);
  if (pose != m_orientation)
    m_ball = {};
  m_orientation = pose;
  m_staleTimer->stop();
  if (pose) {
    m_arrival.start();
    if (!replay)
      m_staleTimer->start(2000);
  }
  setAccessibleDescription(
      pose ? tr("Nose elevation %1 degrees")
                 .arg(std::asin(std::clamp(pose->rotatedVector({0, 1, 0}).y(),
                                           -1.0F, 1.0F)) *
                          180 / std::numbers::pi,
                      0, 'f', 1)
           : tr("No supplied orientation"));
  update();
}
void VisualHorizonWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QColor text(Theme::kTextPrimary()), muted(Theme::kTextMid());
  const QColor background(Theme::kBgBase());
  const QColor markings =
      text.lightnessF() > background.lightnessF() ? text : background;
  painter.setPen(QPen(QColor(Theme::kBorderSubtle()), 1));
  painter.setBrush(QColor(Theme::kBgPanel()));
  painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5),
                          Theme::kRadiusMd, Theme::kRadiusMd);
  QFont font(Theme::kFontMono);
  font.setPixelSize(10);
  font.setBold(true);
  painter.setFont(font);
  painter.setPen(text);
  painter.drawText(QRect(8, 8, width() - 16, 16), Qt::AlignLeft, tr("HORIZON"));
  const bool stale = m_orientation && !m_replay && m_arrival.elapsed() >= 2000;
  painter.setPen(stale ? QColor(Theme::kWarning()) : muted);
  painter.drawText(QRect(8, 8, width() - 16, 16), Qt::AlignRight,
                   !m_orientation ? QString()
                   : m_replay     ? tr("Replay")
                   : stale        ? tr("Stale")
                                  : tr("Live"));
  const int diameter = std::min(width() - 24, height() - 66);
  const double radius = diameter / 2.0;
  const QPointF center(width() / 2.0, 34 + radius);
  const QRectF ballRect(center.x() - radius, center.y() - radius, diameter,
                        diameter);
  painter.setPen(QPen(QColor(Theme::kBorderLight()), 2));
  painter.setBrush(QColor(Theme::kBgDark()));
  painter.drawEllipse(ballRect);
  if (m_orientation) {
    if (m_ball.size() != QSize(diameter, diameter)) {
      m_ball = QImage(diameter, diameter, QImage::Format_ARGB32_Premultiplied);
      m_ball.fill(Qt::transparent);
      const cosmo::gui::NavballFrame frame(*m_orientation);
      const QColor sky(Theme::kInfo()),
          ground = QColor(Theme::kWarning()).darker(240);
      for (int y = 0; y < diameter; ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(m_ball.scanLine(y));
        for (int x = 0; x < diameter; ++x) {
          const float sx = static_cast<float>((x + .5 - radius) / radius),
                      sy = static_cast<float>((radius - y - .5) / radius);
          if (const auto ray = frame.ray(sx, sy)) {
            const double shade =
                .68 + .32 * std::sqrt(std::max(0.0F, 1 - sx * sx - sy * sy));
            QColor color = ray->y() >= 0 ? sky : ground;
            color = color.darker(static_cast<int>(100 / shade));
            if (std::abs(ray->y()) < .012)
              color = markings;
            pixels[x] = color.rgba();
          }
        }
      }
    }
    painter.setOpacity(stale ? .55 : 1);
    painter.drawImage(ballRect, m_ball);
    const auto inverse = m_orientation->conjugated();
    QColor grid = markings;
    grid.setAlpha(180);
    painter.setPen(QPen(grid, .7));
    painter.setBrush(Qt::NoBrush);
    for (int latitude = -60; latitude <= 60; latitude += 30) {
      QPainterPath path;
      bool open = false;
      float bestFacing = -1;
      QPointF labelPoint;
      const double pitch = latitude * std::numbers::pi / 180;
      for (int longitude = 0; longitude <= 360; ++longitude) {
        const double angle = longitude * std::numbers::pi / 180;
        const auto body = inverse.rotatedVector(
            {static_cast<float>(std::cos(pitch) * std::sin(angle)),
             static_cast<float>(std::sin(pitch)),
             static_cast<float>(-std::cos(pitch) * std::cos(angle))});
        if (body.y() < .01) {
          open = false;
          continue;
        }
        const QPointF point =
            center + QPointF(body.x() * radius, body.z() * radius);
        if (!open)
          path.moveTo(point);
        else
          path.lineTo(point);
        open = true;
        if (body.y() > bestFacing &&
            ballRect.adjusted(12, 12, -12, -12).contains(point)) {
          bestFacing = body.y();
          labelPoint = point;
        }
      }
      painter.drawPath(path);
      if (latitude != 0 && bestFacing > 0)
        painter.drawText(
            QRectF(labelPoint.x() - 12, labelPoint.y() - 7, 24, 14),
            Qt::AlignCenter, QString::number(latitude));
    }
    grid.setAlpha(90);
    painter.setPen(QPen(grid, .6));
    for (int meridian = 0; meridian < 180; meridian += 60) {
      QPainterPath path;
      bool open = false;
      const double angle = meridian * std::numbers::pi / 180;
      for (int latitude = -180; latitude <= 180; ++latitude) {
        const double pitch = latitude * std::numbers::pi / 180;
        const auto body = inverse.rotatedVector(
            {static_cast<float>(std::cos(pitch) * std::sin(angle)),
             static_cast<float>(std::sin(pitch)),
             static_cast<float>(-std::cos(pitch) * std::cos(angle))});
        if (body.y() < .01) {
          open = false;
          continue;
        }
        const QPointF point =
            center + QPointF(body.x() * radius, body.z() * radius);
        if (open)
          path.lineTo(point);
        else
          path.moveTo(point);
        open = true;
      }
      painter.drawPath(path);
    }
    painter.setOpacity(1);
  }
  painter.setPen(QPen(muted, 1));
  for (const int angle : {-60, -30, 0, 30, 60}) {
    const double a = angle * std::numbers::pi / 180;
    const QPointF direction(std::sin(a), -std::cos(a));
    painter.drawLine(center + direction * (radius + 2),
                     center + direction * (radius + 6));
  }
  painter.setPen(
      QPen(QColor(Theme::kWarning()), 2.5, Qt::SolidLine, Qt::RoundCap));
  painter.drawLine(center + QPointF(-20, 0), center + QPointF(-7, 0));
  painter.drawLine(center + QPointF(7, 0), center + QPointF(20, 0));
  painter.drawLine(center + QPointF(-7, 0), center + QPointF(0, 5));
  painter.drawLine(center + QPointF(0, 5), center + QPointF(7, 0));
  painter.setPen(text);
  font.setBold(false);
  painter.setFont(font);
  const auto nose =
      m_orientation ? m_orientation->rotatedVector({0, 1, 0}) : QVector3D{};
  painter.drawText(QRect(4, height() - 27, width() - 8, 19), Qt::AlignCenter,
                   m_orientation
                       ? tr("Elevation %1°")
                             .arg(std::asin(std::clamp(nose.y(), -1.0F, 1.0F)) *
                                      180 / std::numbers::pi,
                                  0, 'f', 0)
                       : tr("—"));
}
