#ifndef COSMO_SOFT_DASHBOARDPAGE_H
#define COSMO_SOFT_DASHBOARDPAGE_H

#include <QWidget>

// DashboardPage hosts summary tiles and checklists displayed on the main screen.
// The backend can later expose data setters so these labels update with live telemetry.
class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget *parent = nullptr);
    ~DashboardPage() override = default;
};

#endif // COSMO_SOFT_DASHBOARDPAGE_H
