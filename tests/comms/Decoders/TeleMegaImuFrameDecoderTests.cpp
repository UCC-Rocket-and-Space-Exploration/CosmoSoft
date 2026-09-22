#include <iostream>

#include "catch2/catch_test_macros.hpp"
#include "domain/Frame.h"
#include "include/helpers.h"
#include "services/telemetry/FrameDecoderVault.h"
#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"

TEST_CASE("Decodes frame") {
    //Assert

    std::string frame_str = "TELEM 2234127856082D9600A8610000FAFFD4FE6400F401CEFF2C01FA0019007B000000AAAA42";
    std::vector<uint8_t> pack = std::vector<uint8_t>(frame_str.begin(), frame_str.end());
    Frame f{AltosFrame, pack, 8};
    // 00CA
    //Act
    TeleMegaImuFrameDecoder decoder{};
    FlightSample sample = decoder.decode(f);

    //Assert
    std::cout << "Temp: " << sample.temperature;
    REQUIRE(almost_equal(sample.temperature, -0.06));
    REQUIRE(sample.pressure == 2500);
    REQUIRE(sample.timestamp == 22136);

    REQUIRE(sample.acceleration.x == -300);
    REQUIRE(sample.acceleration.y == 100);
    REQUIRE(sample.acceleration.z == 500);

    REQUIRE(sample.angularVelocity.x == -50);
    REQUIRE(sample.angularVelocity.y == 300);
    REQUIRE(sample.angularVelocity.z == 250);
}


void convertStrToVector(std::string& str, std::vector<uint8_t>& out) {
    for (auto s : str) {
        out.push_back(static_cast<uint8_t>(s));
    }
}


TEST_CASE("Vault test") {
    std::cout << "Vault test" << std::endl;
    FrameDecoderVault vault = FrameDecoderVault();
    // std::string frame_str = "TELEM 2234127856082D9600A8610000FAFFD4FE6400F401CEFF2C01FA0019007B000000AAAA42";
    std::string telem_str = "TELEM 229830660804270100011a0000f401400049474e4953000000312e392e31380000ee9235";
    std::vector<uint8_t> data{};
    convertStrToVector(telem_str, data);
    Frame fr{};
    fr.format = AltosFrame;
    fr.data = data;
    auto decoder = vault.select(fr);
    std::cout << typeid(decoder).name() << std::endl;

    auto sample = decoder->decode(fr);
}

void printFlightSample(const FlightSample& sample)
{
    std::cout
        << "========== Flight Sample =========="
        << "\nTimestamp:              " << sample.timestamp
        << "\nRSSI:                   " << sample.rssi

        << "\n\nAngular Velocity:"
        << "\n  X:                    " << sample.angularVelocity.x
        << "\n  Y:                    " << sample.angularVelocity.y
        << "\n  Z:                    " << sample.angularVelocity.z

        << "\n\nAcceleration:"
        << "\n  X:                    " << sample.acceleration.x
        << "\n  Y:                    " << sample.acceleration.y
        << "\n  Z:                    " << sample.acceleration.z

        << "\n\nCoordinates:"
        << "\n  Latitude:             " << sample.coordinates.latitude
        << "\n  Longitude:            " << sample.coordinates.longitude

        << "\n\nAngular Rotation:"
        << "\n  X:                    " << sample.angularRotation.x
        << "\n  Y:                    " << sample.angularRotation.y
        << "\n  Z:                    " << sample.angularRotation.z

        << "\n\nAltitude:               " << sample.altitude
        << "\nPressure:               " << sample.pressure
        << "\nTemperature:            " << sample.temperature
        << "\nBattery Voltage:        " << sample.batteryVoltage
        << "\nDistance From Launch:   " << sample.distanceFromLaunchPoint
        << "\n====================================";
}
TEST_CASE("tele mini v3 decodes sample") {
    std::string sample_line = "TELEM 229830bb1b1102850c2604e8040b2c6581e82469e4e07f71d6145d812a4d81dae50335af";
    std::vector<uint8_t> pack = std::vector<uint8_t>(sample_line.begin(), sample_line.end());
    Frame f{AltosFrame, pack, 8};
    // 00CA
    //Act
    TeleMiniV3FrameDecoder decoder{};
    FlightSample sample = decoder.decode(f);
    printFlightSample(sample);
}