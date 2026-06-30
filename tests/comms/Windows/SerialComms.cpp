#include <iostream>

#include "catch2/catch_test_macros.hpp"
#include "gateway/comms/windows/SerialCommsWindows.h"
#include "../../../include/shared/exceptions/SerialTimeout.h"
#include  "include/SerialWriter.h"

// TEST_CASE("Serial") {
//     std::shared_ptr<SerialCommsWindows> reader_comm = std::make_shared<SerialCommsWindows>("COM1");
//     uint8_t buf[1] = {};
//     std::cout << "serial" << std::endl;
//     // auto read_b = reader_comm->read(buf, 1);
//     REQUIRE_THROWS_AS(reader_comm->read(buf, 1), SerialTimeout);
// }

// TEST_CASE("Serial") {
//     std::shared_ptr<SerialCommsWindows> reader_comm = std::make_shared<SerialCommsWindows>("COM1");
//     std::shared_ptr<SerialCommsWindows> writer_comm = std::make_shared<SerialCommsWindows>("COM2");
//     SerialWriter writer(writer_comm);
//     char msg[] = "newmsg";
//     writer.write(msg);
//     uint8_t buf[8] = {};
//     reader_comm->read(buf, 3);
//     reader_comm->read(buf, 3);
//     REQUIRE(buf[0] == 'm');
//     REQUIRE(buf[1] == 's');
//     REQUIRE(buf[2] == 'g');
//
// }