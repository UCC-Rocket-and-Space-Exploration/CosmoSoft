//
// Created by Lenovo on 1/21/2026.
//

#ifndef COSMO_SOFT_FLIGHTSAMPLE_H
#define COSMO_SOFT_FLIGHTSAMPLE_H
#include "AccelerationCoordinates.h"
#include "AngularVelocityCoordinates.h"
#include "LocationCoordinates.h"

struct FlightSample {
    long launchTimestamp;
    double rssi;
    AngularVelocityCoordinates angularVelocity;
    AccelerationCoordinates acceleration;
    LocationCoordinates coordinates;
    double altitude;
    double pressure;
    double temperature;
    double batteryVoltage;
};
#endif //COSMO_SOFT_FLIGHTSAMPLE_H