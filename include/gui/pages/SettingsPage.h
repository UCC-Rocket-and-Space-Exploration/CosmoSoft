/**
 * @file SettingsPage.h
 * @brief Floating settings window with sidebar navigation.
 *
 * SettingsPage is opened as a top-level window from the toolbar settings button.
 * Settings are persisted to QSettings on close and restored on show via SettingsKeys.h.
 *
 * Sections (sidebar):
 *  - Appearance  — Skin picker.
 *  - Data        — UI sounds toggle.
 *  - Developer   — Debug mode toggle, system info, developer reference.
 *  - About       — Application version, description, license, repository link.
 */

#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QShowEvent;
class QStackedWidget;

/**
 * @class SettingsPage
 * @brief Sidebar-navigated settings window for CosmoSoft appearance,
 *        data preferences, developer tools, and about info.
 */
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;

    /** @brief Returns true when the developer debug mode toggle is checked. */
    [[nodiscard]] bool debugModeEnabled() const;

signals:
    /**
     * @brief Emitted when the debug mode toggle changes.
     * @param enabled New debug mode state.
     */
    void debugModeChanged(bool enabled);

    /** @brief Emitted when the unit system preference changes. */

protected:
    /** @brief Loads persisted settings and restores window geometry on show. */
    void showEvent(QShowEvent *event) override;

    /** @brief Saves current settings and window geometry before closing. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSoundsToggled(bool enabled);
    void onDebugModeToggled(bool enabled);
    void onSkinChanged(int index);
    void onImportSkin();
    void onThemeChanged();

private:
    void buildUi();
    QWidget *buildAppearanceSection();
    QWidget *buildDataSection();
    QWidget *buildDeveloperSection();
    QWidget *buildAboutSection();

    void loadFromSettings();
    void saveToSettings();

    /** @brief Rebuilds the page-local stylesheet from the active theme palette. */
    void refreshStyleSheet();

    /** @brief Applies @p pt as the application-wide font point size. */

    // ── Navigation ────────────────────────────────────────────────────────────
    QListWidget   *m_nav   = nullptr;
    QStackedWidget *m_pages = nullptr;

    // ── Appearance section ────────────────────────────────────────────────────
    QGroupBox   *m_skinGroup     = nullptr;
    QComboBox   *m_skinCombo     = nullptr;
    QPushButton *m_importSkinBtn = nullptr;

    QGroupBox *m_fontGroup     = nullptr;
    QComboBox *m_fontSizeCombo = nullptr;
    QLabel    *m_fontPreview   = nullptr;
    QGroupBox *m_soundGroup    = nullptr;
    QCheckBox *m_uiSoundsCheck = nullptr;

    // ── Developer section ─────────────────────────────────────────────────────
    QCheckBox *m_debugModeCheck = nullptr;
    QLabel    *m_sysInfoLabel   = nullptr;
    QGroupBox *m_sysInfoGroup   = nullptr;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
