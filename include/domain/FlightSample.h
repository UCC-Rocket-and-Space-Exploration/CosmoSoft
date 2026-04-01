#ifndef COSMO_SOFT_DOMAIN_FLIGHTSAMPLE_H
#define COSMO_SOFT_DOMAIN_FLIGHTSAMPLE_H

struct AngularVelocity {
    double x = 0, y = 0, z = 0;
};

struct Acceleration {
    double x = 0, y = 0, z = 0;
};

struct Coordinates {
    double latitude = 0, longitude = 0;
};

struct FlightSample {
    long timestamp = 0;
    double rssi = 0;
    AngularVelocity angularVelocity;
    Acceleration acceleration;
    Coordinates coordinates;
    double altitude = 0;
    double pressure = 0;
    double temperature = 0;
    double batteryVoltage = 0;
};

#endif // COSMO_SOFT_DOMAIN_FLIGHTSAMPLE_H
