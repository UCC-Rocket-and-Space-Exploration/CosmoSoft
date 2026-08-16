// #include "services/RingBuffer.h"
// #include <cassert>
// #include "catch2/catch_test_macros.hpp"
// #include "catch2/internal/catch_stdstreams.hpp"
// #include "domain/Frame.h"
// #include "gateway/comms/windows/SerialCommsWindows.h"
// #include "include/functions.h"
//
// // TEST_CASE("putItemsUntilBufferIsFull_getPutItems") {
// //     //Arrange
// //     // std::cout << "putItemsUntilBufferIsFull_getPutItems:" << '\n';
// //     int buf_size = 10;
// //     RingBuffer<int> b = RingBuffer<int>(buf_size);
// //     for (int i = 0; i < buf_size; ++i) {
// //         b.put((char)i);
// //     }
// //     //Assert
// //     for (int i = 0; i < buf_size; ++i) {
// //         auto actual = b.get();
// //         std::cout << actual.value() << std::endl;
// //         REQUIRE(actual.value() == (char)i);
// //     }
// // }
//
// TEST_CASE("Buffer with frames") {
//
//     RingBuffer<Frame> b = RingBuffer<Frame>(32);
//     char frame_content1[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";
//     char frame_content2[] = "5555,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
//     std::vector<uint8_t> f1_data = std::vector<uint8_t>(frame_content1, frame_content1 + strlen(frame_content1));
//     std::vector<uint8_t> f2_data = std::vector<uint8_t>(frame_content2, frame_content2 + strlen(frame_content2));
//
//     Frame f1(Csv, f1_data, std::strlen(frame_content1));
//     Frame f2(Csv, f2_data, std::strlen(frame_content2));
//     b.put(f1);
//     b.put(f2);
//     auto actual_f1 = b.get();
//     auto actual_f2 = b.get();
//     REQUIRE(actual_f1.has_value());
//     REQUIRE(actual_f2.has_value());
//
//     excepted_actual_content_equal_except_last_char(frame_content1, actual_f1.value().data);
//     std::cout << '\n';
//     excepted_actual_content_equal_except_last_char(frame_content2, actual_f2.value().data);
// }
//
// // TEST_CASE() {
// //     RingBuffer<Frame> b = RingBuffer<Frame>(32);
// //     char frame_content1[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";
// //     char frame_content2[] = "5555,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
// //     Frame f1(FrameFormat::Csv, (uint8_t*)frame_content1, std::strlen(frame_content1));
// //     Frame f2(FrameFormat::Csv, (uint8_t*)frame_content2, std::strlen(frame_content2));
// //
// //     auto a = b.get();
// //     auto val = a.value();
// // }