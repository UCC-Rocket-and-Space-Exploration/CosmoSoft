// #include <iostream>
#include <catch2/catch_test_macros.hpp>

#include "gateway/comms/Windows/SerialCommsWindows.h"
#include "include/SerialWriter.h"
#include "services/telemetry/framers/CsvFramer.h"
#include "services/telemetry/framers/AltosFramer.h"
#include "shared/exceptions/SerialTimeout.h"

// TEST_CASE("get frames with different formats") {
//     std::shared_ptr<SerialCommsWindows> writer_comm = std::make_shared<SerialCommsWindows>("COM2");
//     std::shared_ptr<SerialCommsWindows> reader_comm = std::make_shared<SerialCommsWindows>("COM1");
//
//     SerialWriter writer(writer_comm);
//     AltosFramer framer(reader_comm);
//
//     std::cout << "initilized" << std::endl;
//     //Arrange
//     std::string packet_data = "224f01080b05765e00701f1a1bbeb8d7b60b070605140c000600000000000000003fa988";
//     std::string full_packet = "TELEM " + packet_data;
//     std::string full_packet_without_w = "TELEM" + packet_data;
//
//     //Act
//
//     std::cout << "packet1: " << full_packet << std::endl;
//     writer.write(const_cast<char*>(full_packet.c_str()));
//     std::cout << "packet2: " << full_packet_without_w << std::endl;
//     writer.write(const_cast<char*>(full_packet_without_w.c_str()));
//
//     Frame frame = framer.get_frame(true);
//     Frame frame2 = framer.get_frame(true);
//
//     //Assert
//     REQUIRE(frame.data.size() == full_packet.size());
//     REQUIRE(frame2.data.size() == full_packet_without_w.size());
//
//     std::cout << std::endl;
//     REQUIRE(frame.packet_start_index == 8);
//     REQUIRE(frame2.packet_start_index == 7);
//
//     for (int i = 0; i < full_packet.size(); i++) {
//         std::cout << frame.data[i];
//         REQUIRE(full_packet[i] == frame.data[i]);
//         // REQUIRE(packet_data[i] == frame2.data[i]);
//     }
//
//     std::cout << "\n";
//     for (int i = 0; i < full_packet_without_w.size(); i++) {
//         std::cout << frame2.data[i];
//         REQUIRE(full_packet_without_w[i] == frame2.data[i]);
//         // REQUIRE(packet_data[i] == frame2.data[i]);
//     }
//     std::cout << "\n";
// }


// TEST_CASE("getting csv frame, while cancellation") {
//     std::shared_ptr<SerialCommsWindows> writer_comm = std::make_shared<SerialCommsWindows>("COM2");
//     std::shared_ptr<SerialCommsWindows> reader_comm = std::make_shared<SerialCommsWindows>("COM1");
//
//     SerialWriter writer(writer_comm);
//     TeleFramer framer(reader_comm);
//
//     std::cout << "initilized" << std::endl;
//     //Arrange
//     char content[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00";
//
//     //Act
//     bool running = false;
//     writer.write(content);
//     Frame frame = framer.get_frame(running);
//     REQUIRE(frame.data.empty());
// }
