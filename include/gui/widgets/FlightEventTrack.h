#pragma once
#include <QJsonArray>
#include <QWidget>
#include <functional>

/** @brief Compact, clustered event markers synchronized to the replay timeline.
 */
class FlightEventTrack : public QWidget {
public:
  /** @brief Create the event track without allocating another chart. */
  explicit FlightEventTrack(QWidget *parent = nullptr);
  /** @brief Replace annotations and the full recording duration. */
  void setEvents(const QJsonArray &events, double duration);
  /** @brief Update the timeline extent without rebuilding event controls. */
  void setDuration(double duration);
  /** @brief Request seeking to a clicked event time in seconds. */
  std::function<void(double)> eventActivated;

protected:
  /** @brief Paint nearby event markers as clusters with counts. */
  void paintEvent(QPaintEvent *event) override;
  /** @brief Select the event closest to the clicked timeline position. */
  void mousePressEvent(QMouseEvent *event) override;
  /** @brief Navigate original events with the keyboard. */
  void keyPressEvent(QKeyEvent *event) override;

private:
  QJsonArray m_events;
  double m_duration = 1;
  int m_selected = -1;
};
