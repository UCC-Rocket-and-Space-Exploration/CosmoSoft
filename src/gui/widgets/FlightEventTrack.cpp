#include "gui/widgets/FlightEventTrack.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

FlightEventTrack::FlightEventTrack(QWidget *parent) : QWidget(parent) {
  setObjectName("flightEventTrack");
  setFixedHeight(26);
  setMinimumWidth(80);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(tr("Flight events timeline"));
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, [this] { update(); });
}
void FlightEventTrack::setEvents(const QJsonArray &events, double duration) {
  m_events = events;
  m_duration = std::max(.001, duration);
  m_selected = -1;
  update();
}
void FlightEventTrack::setDuration(double duration) {
  const double bounded = std::max(.001, duration);
  if (m_duration == bounded)
    return;
  m_duration = bounded;
  update();
}
void FlightEventTrack::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(Theme::kBorderLight()), 1));
  painter.drawLine(8, 18, width() - 8, 18);
  auto font = painter.font();
  font.setPixelSize(10);
  painter.setFont(font);
  for (qsizetype i = 0; i < m_events.size();) {
    const double x =
        8 + std::clamp(m_events.at(i).toObject().value("time").toDouble() /
                           m_duration,
                       0.0, 1.0) *
                (width() - 16);
    qsizetype end = i + 1;
    while (end < m_events.size() &&
           (m_events.at(end).toObject().value("time").toDouble() -
            m_events.at(i).toObject().value("time").toDouble()) /
                   m_duration * (width() - 16) <
               96)
      ++end;
    painter.setPen(QPen(QColor(Theme::kAccentLink()), 2));
    painter.drawLine(QPointF(x, 13), QPointF(x, 23));
    painter.setPen(QColor(Theme::kTextPrimary()));
    painter.drawText(
        QRectF(std::clamp(x - 45, 0.0, std::max(0.0, width() - 90.0)), 0, 90,
               13),
        Qt::AlignCenter,
        end - i > 1
            ? QString::number(end - i)
            : m_events.at(i).toObject().value("name").toString().left(14));
    i = end;
  }
}
void FlightEventTrack::mousePressEvent(QMouseEvent *event) {
  if (m_events.isEmpty())
    return;
  const double time =
      (event->position().x() - 8) / std::max(1, width() - 16) * m_duration;
  int nearest = 0;
  double distance = std::numeric_limits<double>::infinity();
  for (qsizetype i = 0; i < m_events.size(); ++i) {
    const double delta =
        std::abs(time - m_events.at(i).toObject().value("time").toDouble());
    if (delta < distance) {
      distance = delta;
      nearest = static_cast<int>(i);
    }
  }
  m_selected = nearest;
  if (eventActivated)
    eventActivated(m_events.at(nearest).toObject().value("time").toDouble());
}
void FlightEventTrack::keyPressEvent(QKeyEvent *event) {
  if (!m_events.isEmpty() &&
      (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
    m_selected =
        std::clamp(m_selected + (event->key() == Qt::Key_Right ? 1 : -1), 0,
                   static_cast<int>(m_events.size()) - 1);
    if (eventActivated)
      eventActivated(
          m_events.at(m_selected).toObject().value("time").toDouble());
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}
