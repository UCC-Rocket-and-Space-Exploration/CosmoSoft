#ifndef COSMO_SOFT_ALTOSFRAMETYPES_H
#define COSMO_SOFT_ALTOSFRAMETYPES_H
enum AltosPacketTypes {
    Gps = 0x05,
    Companion = 0x07,
    Config = 0x04,
    TeleMiniV3 = 0x11,
    TeleMiniV1 = 0x02,
    TeleMegaInvensenseIMU = 0x08,
    TeleMegaBMX160IMU = 0x12,
    TeleMegaMPU6000IMU = 0x13,
    TeleMegaBMI088IMU = 0x14,
    TeleMega15VKalman = 0x09,
    TeleMega30VKalman = 0x15,
    Unknown
};
#endif //COSMO_SOFT_ALTOSFRAMETYPES_H