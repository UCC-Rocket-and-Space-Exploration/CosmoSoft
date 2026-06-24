#ifndef COSMO_SOFT_FLIGHTSAMPLE_H
#define COSMO_SOFT_FLIGHTSAMPLE_H


struct GpsCoordinate {
    double x;
    double y;
    double z;
};

struct AngularVelocity {
    double x = 0, y = 0, z = 0;
};

struct Acceleration {
    double x = 0, y = 0, z = 0;
};
struct AngularOffsetCoordinates {
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
    AngularOffsetCoordinates angularRotation;
    double altitude = 0; // TODO: mb should be going into Coordinates?
    double pressure = 0;
    double temperature = 0;
    double batteryVoltage = 0;
    double distanceFromLaunchPoint = 0;
};

// struct FlightSample {
//     long launchTimestamp;
//     double rssi;
//     AngularVelocity angularVelocity;
//     Acceleration acceleration;
//     GpsCoordinate coordinates;
//     double altitude;
//     double pressure;
//     double temperature;
//     double batteryVoltage;
//     unsigned int errorFlags : 9; //TODO define error flags
//
//     explicit operator bool() const {
//         return errorFlags == 0;
//     }
// };

#endif //COSMO_SOFT_FLIGHTSAMPLE_H