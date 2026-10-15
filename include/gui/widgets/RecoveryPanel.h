#pragma once
#include <QElapsedTimer>
#include <QFrame>
#include <QJsonObject>
#include <functional>
class QLabel;
class QAction;
class QTimer;

/** @brief Compact map mode control and last-known-position recovery overlay. */
class RecoveryPanel : public QFrame {
public:
  /** @brief Construct recovery details, hidden until the host enables Recovery.
   */
  explicit RecoveryPanel(QWidget *parent = nullptr);
  /** @brief Shared checkable mode action for the host action bar. */
  [[nodiscard]] QAction *modeAction() const { return m_mode; }
  /** @brief Update coordinates without inventing a landing position or
   * timestamp. */
  void setRecord(const QJsonObject &record);
  /** @brief Restore the named launch/ground reference from a flight sidecar. */
  void setReference(const QJsonObject &reference);
  /** @brief Current explicitly chosen ground reference, if any. */
  [[nodiscard]] QJsonObject reference() const { return m_reference; }
  /** @brief Notify the host map of mode changes and coordinates through its
   * bridge. */
  std::function<void(bool, const QJsonObject &)> modeChanged;
  /** @brief Notify the recording owner of an edited ground reference. */
  std::function<void()> referenceChanged;

protected:
  /** @brief Resume receipt-age refresh when the panel becomes visible. */
  void showEvent(QShowEvent *event) override;
  /** @brief Suspend periodic work while the map is hidden. */
  void hideEvent(QHideEvent *event) override;

private:
  void refresh();
  void refreshStyleSheet();
  void chooseReference();
  QJsonObject m_record, m_reference;
  QAction *m_mode = nullptr;
  QWidget *m_details = nullptr;
  QLabel *m_position = nullptr;
  QTimer *m_timer = nullptr;
  QElapsedTimer m_receiptAge;
  qint64 m_initialAgeMs = 0;
};
