#include <iostream>

#include "catch2/catch_test_macros.hpp"
#include "domain/Frame.h"
#include "include/helpers.h"
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
