#ifndef COSMO_SOFT_SETTINGSPAGE_H
#define COSMO_SOFT_SETTINGSPAGE_H

#include <QWidget>

// FlightDataPage demonstrates forms for configuring comms and appearance preferences.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);
    ~SettingsPage() override = default;
};

#endif // COSMO_SOFT_SETTINGSPAGE_H
