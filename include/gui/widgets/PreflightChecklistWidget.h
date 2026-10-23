#pragma once
#include <QFrame>
#include <QJsonArray>
#include <functional>
#include <optional>
class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;
class QTableWidget;
class QLineEdit;

/** @brief Collapsible manual and measured checks, with per-session
 * confirmations. */
class PreflightChecklistWidget : public QFrame {
public:
  /** @brief Construct a compact checklist summary, collapsed by default. */
  explicit PreflightChecklistWidget(QWidget *parent = nullptr);
  /** @brief Reload saved profiles, preserving confirmations only if definitions
   * match. */
  void reloadProfiles();
  /** @brief Clear manual confirmations for a new recording. */
  void resetChecks();
  /** @brief Refresh known measurements; unavailable source fields stay unknown.
   */
  void setMeasurements(std::optional<double> packetAge,
                       std::optional<double> battery, bool position);

private:
  void rebuild();
  void refreshStyleSheet();
  QJsonArray m_profiles;
  QComboBox *m_profile = nullptr;
  QListWidget *m_items = nullptr;
  QLabel *m_summary = nullptr;
  QToolButton *m_toggle = nullptr;
  QWidget *m_details = nullptr;
  std::optional<double> m_age, m_battery;
  bool m_position = false;
};

/** @brief Settings editor for bounded, named preflight profiles. */
class PreflightProfileEditor : public QFrame {
public:
  /** @brief Construct a profile editor that saves through the existing settings
   * store. */
  explicit PreflightProfileEditor(QWidget *parent = nullptr);
  /** @brief Notify the live checklist after successful profile persistence. */
  std::function<void()> profilesSaved;

private:
  void capture();
  void display(int index);
  void refreshProfiles(int selected);
  void refreshStyleSheet();
  QJsonArray m_profiles;
  QComboBox *m_selector = nullptr;
  QLineEdit *m_name = nullptr;
  QTableWidget *m_table = nullptr;
  int m_index = 0;
  bool m_loading = false;
};
