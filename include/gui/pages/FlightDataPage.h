#ifndef COSMO_SOFT_FLIGHTDATAPAGE_H
#define COSMO_SOFT_FLIGHTDATAPAGE_H

#include <QWidget>

// FlightDataPage demonstrates forms for configuring comms and appearance preferences.
class FlightDataPage : public QWidget {
    Q_OBJECT

public:
    explicit FlightDataPage(QWidget *parent = nullptr);
    ~FlightDataPage() override = default;
};

#endif // COSMO_SOFT_FLIGHTDATAPAGE_H
