#include "catch2/catch_test_macros.hpp"
#include "gateway/comms/windows/SerialCommsWindows.h"
#include "include/helpers.h"
#include "services/telemetry/ParserWorker.h"
#include "Mocks/onDataMock.cpp"

// TEST_CASE("Added two frames into the buffer, parses them and returns two flight_samples") {
//     std::shared_ptr<RingBuffer<Frame>> buffer = std::make_shared<RingBuffer<Frame>>(5);
//     void (*onDataCallback)(const FlightSample& f_s) = onData;
//
//     ParserWorker worker(buffer, onDataCallback);
//
//     char frame_content1[] = "0,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00";
//     char frame_content2[] = "1000,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.50";
//     std::vector<uint8_t> f1_data = std::vector<uint8_t>(frame_content1, frame_content1 + strlen(frame_content1));
//     std::vector<uint8_t> f2_data = std::vector<uint8_t>(frame_content2, frame_content2 + strlen(frame_content2));
//
//     const Frame f1(Csv, f1_data);
//     Frame f2(Csv, f2_data);
//
//     buffer->put(f1);
//     buffer->put(f2);
//
//     worker.start();
//     Sleep(1000);
//     worker.stop();
//
//     REQUIRE(parsed_samples.size() == 2);
//     REQUIRE(parsed_samples[0].timestamp == 0);
//     REQUIRE(parsed_samples[1].timestamp == 1000);
//     REQUIRE(almost_equal(parsed_samples[1].distanceFromLaunchPoint, 0.5));
// }
