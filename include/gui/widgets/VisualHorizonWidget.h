#pragma once

#include "domain/FlightSample.h"
#include <QElapsedTimer>
#include <QFrame>
#include <QImage>
#include <QQuaternion>
#include <optional>
class QTimer;

/** @brief Independent embedded navball, looking along the rocket's +Y nose. */
class VisualHorizonWidget : public QFrame {
public:
  /** @brief Construct a theme-aware, software-painted horizon module. */
  explicit VisualHorizonWidget(QWidget *parent = nullptr);
  /** @brief Display supplied orientation; replay does not expire while paused.
   */
  void setSample(const FlightSample &sample, bool replay = false);
  /** @brief Clear the pose without displaying an invented orientation. */
  void resetDisplay();

protected:
  /** @brief Paint the sphere, fixed vehicle indicator and reference scales. */
  void paintEvent(QPaintEvent *event) override;

private:
  void refreshTheme();
  QImage m_ball;
  std::optional<QQuaternion> m_orientation;
  QElapsedTimer m_arrival;
  QTimer *m_staleTimer = nullptr;
  bool m_replay = false;
};
