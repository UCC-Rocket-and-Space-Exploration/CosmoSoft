// #include <iostream>
//
// #include "catch2/catch_test_macros.hpp"
// #include "include/helpers.h"
// #include "services/telemetry/frame_decoders/CsvFrameDecoder.h"
// CsvFrameDecoder decoder{};
//
// TEST_CASE("CsvDecoder decodes frame") {
//     const char* data_string =
//             "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.100000,0.01";
//
//     std::vector<uint8_t> data = {};
//     data.assign(data_string, data_string + strlen(data_string));
//     Frame frame(Csv, data);
//
//     FlightSample f_sample = decoder.decode(frame);
//     std::cout << f_sample.pressure << std::endl;
//     std::cout << f_sample.temperature << std::endl;
//
//     REQUIRE(f_sample.timestamp == 13603);
//     REQUIRE(almost_equal(f_sample.temperature, 19.24));
//     REQUIRE(almost_equal(f_sample.pressure, 100896.15));
//
//     REQUIRE(almost_equal(f_sample.altitude, 165.59));
//
//     REQUIRE(f_sample.acceleration.x == 0.704);
//     REQUIRE(f_sample.acceleration.y == -0.927);
//     REQUIRE(f_sample.acceleration.z == 9.850);
//
//     REQUIRE(f_sample.angularVelocity.x == -0.047);
//     REQUIRE(f_sample.angularVelocity.y == 0.091);
//     REQUIRE(f_sample.angularVelocity.z == 0.091);
//
//     REQUIRE(f_sample.angularRotation.x == -4.822);
//     REQUIRE(f_sample.angularRotation.y == -2.985);
//     REQUIRE(f_sample.angularRotation.z == 16.506);
//
//     REQUIRE(f_sample.coordinates.latitude == 0);
//     REQUIRE(f_sample.coordinates.longitude == 0.1);
//
//     REQUIRE(f_sample.distanceFromLaunchPoint == 0.01);
//
//     REQUIRE(f_sample.rssi == 0);
//
// }
//
// TEST_CASE("Incorrect data") {
//     const char* data_string =
//             "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.100000,0.01";
//
//     std::vector<uint8_t> data = {};
//     data.assign(data_string, data_string + strlen(data_string));
//     Frame frame(Csv, data);
//
//     FlightSample f_sample = decoder.decode(frame);
//     std::cout << f_sample.pressure << std::endl;
//     std::cout << f_sample.temperature << std::endl;
//
//     REQUIRE(f_sample.timestamp == 13603);
//     REQUIRE(almost_equal(f_sample.temperature, 19.24));
//     REQUIRE(almost_equal(f_sample.pressure, 100896.15));
//
//     REQUIRE(almost_equal(f_sample.altitude, 165.59));
//
//     REQUIRE(f_sample.acceleration.x == 0.704);
//     REQUIRE(f_sample.acceleration.y == -0.927);
//     REQUIRE(f_sample.acceleration.z == 9.850);
//
//     REQUIRE(f_sample.angularVelocity.x == -0.047);
//     REQUIRE(f_sample.angularVelocity.y == 0.091);
//     REQUIRE(f_sample.angularVelocity.z == 0.091);
//
//     REQUIRE(f_sample.angularRotation.x == -4.822);
//     REQUIRE(f_sample.angularRotation.y == -2.985);
//     REQUIRE(f_sample.angularRotation.z == 16.506);
//
//     REQUIRE(f_sample.coordinates.latitude == 0);
//     REQUIRE(f_sample.coordinates.longitude == 0.1);
//
//     REQUIRE(f_sample.distanceFromLaunchPoint == 0.01);
//
//     REQUIRE(f_sample.rssi == 0);
// }