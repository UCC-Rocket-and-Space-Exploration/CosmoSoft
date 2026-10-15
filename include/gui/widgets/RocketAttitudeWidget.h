/** @file RocketAttitudeWidget.h
 * @brief Compact rocket attitude panel shared by Replay and Live Telemetry.
 */
#ifndef COSMO_SOFT_ROCKETATTITUDEWIDGET_H
#define COSMO_SOFT_ROCKETATTITUDEWIDGET_H

#include "domain/FlightSample.h"
#include <QElapsedTimer>
#include <QFrame>

class QLabel;
class QTimer;
class RocketAttitudeCanvas;

/** @brief Embedded instrument showing a rocket against fixed reference axes. */
class RocketAttitudeWidget : public QFrame {
public:
  /** @brief Create an embedded panel; the host page supplies its samples. */
  explicit RocketAttitudeWidget(QWidget *parent = nullptr);
  /** @brief Display the exact sample pose; replay poses do not expire while
   * paused. */
  void setSample(const FlightSample &sample, bool replay = false);
  /** @brief Clear the pose and all angle readouts. */
  void resetDisplay();
  /** @brief Use a narrow layout beside a separate horizon module. */
  void setCompact(bool compact);

private:
  void refreshStyleSheet();
  void refreshStatus();
  RocketAttitudeCanvas *m_canvas = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_angles = nullptr;
  QTimer *m_staleTimer = nullptr;
  QElapsedTimer m_lastUpdate;
  bool m_compact = false;
  bool m_replay = false;
  bool m_available = false;
};
#endif
