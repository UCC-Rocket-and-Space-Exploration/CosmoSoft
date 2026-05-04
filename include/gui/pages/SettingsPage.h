/**
 * @file SettingsPage.h
 * @brief Floating settings window with General, About, and Developer tabs.
 *
 * SettingsPage is opened as a top-level window from the toolbar settings button.
 * Settings are persisted to QSettings on close and restored on show via SettingsKeys.h.
 *
 * Tabs:
 *  - General  — UI font size, UI sounds toggle.
 *  - About    — Application version, description, license, repository link.
 *  - Developer — Debug mode toggle, system info, event log, developer helper.
 */

#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QGroupBox;
class QLabel;
class QShowEvent;
class QTabWidget;
class EventLogPage;

/**
 * @class SettingsPage
 * @brief Tabbed settings window for CosmoSoft appearance, about info, and developer tools.
 */
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;

    /** @brief Returns true when the developer debug mode toggle is checked. */
    [[nodiscard]] bool debugModeEnabled() const;

public slots:
    /**
     * @brief Appends a timestamped informational entry to the embedded event log.
     * @param text Human-readable event description.
     */
    void appendLogEntry(const QString &text);

    /**
     * @brief Appends a timestamped error entry (shown in red) to the embedded event log.
     * @param text Human-readable error description.
     */
    void appendLogError(const QString &text);

signals:
    /**
     * @brief Emitted when the debug mode toggle changes.
     * @param enabled New debug mode state.
     */
    void debugModeChanged(bool enabled);

    /** @brief Emitted when the unit system preference changes. */
    void unitSystemChanged(const QString &system);

protected:
    /** @brief Loads persisted settings and restores window geometry on show. */
    void showEvent(QShowEvent *event) override;

    /** @brief Saves current settings and window geometry before closing. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFontSizeChanged(int index);
    void onUnitSystemChanged(int index);
    void onSoundsToggled(bool enabled);
    void onDebugModeToggled(bool enabled);

private:
    void buildUi();
    QWidget *buildGeneralTab();
    QWidget *buildAboutTab();
    QWidget *buildDeveloperTab();

    void loadFromSettings();
    void saveToSettings();

    /** @brief Applies @p pt as the application-wide font point size. */
    void applyFontPointSize(int pt);

    // ── Tab container ─────────────────────────────────────────────────────────
    QTabWidget *m_tabs = nullptr;

    // ── General tab ───────────────────────────────────────────────────────────
    QGroupBox *m_fontGroup        = nullptr;
    QComboBox *m_fontSizeCombo    = nullptr;
    QLabel    *m_fontPreview      = nullptr;

    QGroupBox *m_unitsGroup       = nullptr;
    QComboBox *m_unitSystemCombo  = nullptr;

    QGroupBox *m_soundGroup       = nullptr;
    QCheckBox *m_uiSoundsCheck    = nullptr;

    // ── Developer tab ─────────────────────────────────────────────────────────
    QCheckBox    *m_debugModeCheck = nullptr;
    QLabel       *m_sysInfoLabel   = nullptr;
    QGroupBox    *m_sysInfoGroup   = nullptr;
    EventLogPage *m_eventLog       = nullptr;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
