#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QStringList>
#include <QWidget>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;

    void setPortNames(const QStringList &ports);

    [[nodiscard]] QString replayDirectory() const;
    void setSerialLinkStatus(const QString &text);

signals:
    void refreshPortsRequested();
    void connectRequested(const QString &portName, int baudRate);
    void disconnectRequested();
    void openReplayFileRequested();
    void clearFlightDataRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void saveConnectionFields();

private:
    void buildUi();
    void loadFromSettings();
    void saveToSettings();

    QGroupBox *m_connectionGroup = nullptr;
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QLabel *m_linkStatus = nullptr;

    QGroupBox *m_replayGroup = nullptr;
    QLineEdit *m_replayDirEdit = nullptr;

    QGroupBox *m_appGroup = nullptr;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
