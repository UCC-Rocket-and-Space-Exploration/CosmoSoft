#ifndef COSMO_SOFT_FLIGHTSAMPLE_H
#define COSMO_SOFT_FLIGHTSAMPLE_H

struct AngularVelocity {
    double x;
    double y;
    double z;
};

struct Acceleration {
    double x;
    double y;
    double z;
};

struct GpsCoordinate {
    double x;
    double y;
};

struct FlightSample {
    long launchTimestamp;
    double rssi;
    AngularVelocity angularVelocity;
    Acceleration acceleration;
    GpsCoordinate coordinates;
    double altitude;
    double pressure;
    double temperature;
    double batteryVoltage;
    unsigned int errorFlags : 9; //TODO define error flags

    explicit operator bool() const {
        return errorFlags == 0;
    }
};

#endif //COSMO_SOFT_FLIGHTSAMPLE_H