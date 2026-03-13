#ifndef COSMO_SOFT_FLIGHTDATAMODEL_H
#define COSMO_SOFT_FLIGHTDATAMODEL_H
#include <format>
#include <vector>

struct AngularVelocity {
    double x, y, z;
};

struct Acceleration {
    double x, y, z;
};

struct Coordinates {
    double latitude, longitude;
};

struct FlightSample {
    long timestamp; //milliseconds from epoch
    double Rssi;
    AngularVelocity angularVelocity;
    Acceleration acceleration;
    Coordinates coordinates;
    double altitude;
    double pressure;
    double temperature;
    double batteryVoltage;
};

//data model for the UI to use; contains all the formulated data for use by the GUI controller
class FlightDataModel {
public:
    FlightDataModel();
    ~FlightDataModel();

private:
    std::vector<FlightSample> m_flightSamples;
};

#endif //COSMO_SOFT_FLIGHTDATAMODEL_H