#ifndef COSMO_SOFT_MONITORINGPAGE_H
#define COSMO_SOFT_MONITORINGPAGE_H

#include <QWidget>
#include <QStringList>

class QPaintEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QPlainTextEdit;

class MainWindow;

// MonitoringPage hosts summary tiles and checklists displayed on the main screen.
// The backend can later expose data setters so these labels update with live telemetry.
class MonitoringPage : public QWidget {
    Q_OBJECT

public:
    explicit MonitoringPage(MainWindow *hostWindow, QWidget *parent = nullptr);
    ~MonitoringPage() override = default;

signals:
    void scanPortsRequested();
    void connectToPortRequested(const QString &portName);

public slots:
    void showAvailablePorts(const QStringList &ports);
    void appendSerialLog(const QString &text);
    void showSerialMonitor(); // Maintained for compatibility; now focuses the in-page monitor.

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    MainWindow *m_hostWindow = nullptr;
    QListWidget *m_portList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPlainTextEdit *m_logView = nullptr;
};

#endif // COSMO_SOFT_MONITORINGPAGE_H
