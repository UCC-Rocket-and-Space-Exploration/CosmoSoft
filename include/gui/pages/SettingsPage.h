/**
 * @file SettingsPage.h
 * @brief Floating settings window for appearance and audio preferences.
 *
 * SettingsPage is opened as a top-level window (not a stacked page) from the
 * toolbar settings button.  Settings are persisted to QSettings on close and
 * restored on show via SettingsKeys.h constants.
 *
 * Current options:
 *  - UI font size (9–18 pt), applied immediately via qApp->setFont().
 *  - UI sounds toggle (reserved; no audio engine is wired yet).
 */

#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QWidget>

class QCloseEvent;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QShowEvent;

/**
 * @class SettingsPage
 * @brief Scrollable settings window for CosmoSoft appearance and audio options.
 */
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;

protected:
    /** @brief Loads persisted settings and restores window geometry on show. */
    void showEvent(QShowEvent *event) override;

    /** @brief Saves current settings and window geometry before closing. */
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFontSizeChanged(int index);
    void onSoundsToggled(bool enabled);

private:
    void buildUi();
    void loadFromSettings();
    void saveToSettings();

    /** @brief Applies @p pt as the application-wide font point size. */
    void applyFontPointSize(int pt);

    QGroupBox *m_fontGroup     = nullptr;
    QComboBox *m_fontSizeCombo = nullptr;

    QGroupBox *m_soundGroup    = nullptr;
    QCheckBox *m_uiSoundsCheck = nullptr;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
