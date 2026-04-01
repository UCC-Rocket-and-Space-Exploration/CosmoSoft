#ifndef COSMO_SOFT_MAINWINDOW_H
#define COSMO_SOFT_MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include <memory>

#include "domain/FlightSession.h"
#include "services/BlockingQueue.h"

class QAction;
class QComboBox;
class QLabel;
class QStackedWidget;
class QTimer;
class MonitoringPage;
class DashboardPage;
class SettingsPage;
class FlightDataModel;
class FlightReplayController;
class ParserWorker;
class SerialWorker;
class IComms;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showStatusMessage(const QString &message, int timeout = 0);

public slots:
    void onParserError(const QString &message);

private slots:
    void updateMissionClock();
    void updateDataRateLabel();
    void refreshSerialPorts();
    void startSerial(const QString &portName, int baud);
    void stopSerial();
    void onReplayPositionChanged(int trailLength);
    void onOpenReplayFile();
    void onClearFlightData();

private:
    void setupActions();
    void setupToolbar();
    void setupDataBar();
    void setupConnectionBar();
    void loadSerialPrefsToUi();
    void persistSerialPrefs();
    void setupPages();
    void openSettingsWindow();

    QAction *m_showMonitoringAction = nullptr;
    QAction *m_showFlightDataAction = nullptr;
    QAction *m_openSettingsAction = nullptr;

    QStackedWidget *m_pages = nullptr;
    MonitoringPage *m_monitoringPage = nullptr;
    DashboardPage *m_flightDataPage = nullptr;
    SettingsPage *m_settingsWindow = nullptr;

    QLabel *m_missionMetaLabel = nullptr;
    QTimer *m_missionClockTimer = nullptr;

    QWidget *m_connectionBar = nullptr;
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;

    QWidget *m_dataBar = nullptr;
    QLabel *m_dataLinkStatusLabel = nullptr;
    QLabel *m_dataRateLabel = nullptr;

    std::unique_ptr<FlightDataModel> m_flightModel;
    std::unique_ptr<FlightReplayController> m_replay;
    FlightSession m_loadedSession;

    BlockingQueue<std::vector<uint8_t>> m_rawQueue{512};
    std::unique_ptr<ParserWorker> m_parserWorker;
    std::unique_ptr<SerialWorker> m_serialWorker;
    std::unique_ptr<IComms> m_comms;

    QTimer *m_dataRateTimer = nullptr;
    qint64 m_prevBytesForRate = 0;
};

#endif // COSMO_SOFT_MAINWINDOW_H
