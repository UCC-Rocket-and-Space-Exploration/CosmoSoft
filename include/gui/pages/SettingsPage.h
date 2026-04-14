#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QWidget>

class QCloseEvent;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QShowEvent;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onFontSizeChanged(int index);
    void onSoundsToggled(bool enabled);

private:
    void buildUi();
    void loadFromSettings();
    void saveToSettings();
    void applyFontPointSize(int pt);

    QGroupBox *m_fontGroup = nullptr;
    QComboBox *m_fontSizeCombo = nullptr;

    QGroupBox *m_soundGroup = nullptr;
    QCheckBox *m_uiSoundsCheck = nullptr;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
