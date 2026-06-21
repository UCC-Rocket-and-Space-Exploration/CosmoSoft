#include <iostream>
#include <catch2/catch_test_macros.hpp>

#include "gateway/comms/windows/SerialCommsWindows.h"
#include "include/SerialWriter.h"
#include "services/telemetry/CsvFramer.h"

std::unique_ptr<SerialCommsWindows> writer_comm = std::make_unique<SerialCommsWindows>("COM2");
std::unique_ptr<SerialCommsWindows> reader_comm = std::make_unique<SerialCommsWindows>("COM1");

SerialWriter writer(std::move(writer_comm));
CsvFramer framer(std::move(reader_comm));

TEST_CASE("getting frame with \\n char only") {
    char content[2] = "\n";

    writer.write(content);
    Frame frame = framer.get_frame();

    REQUIRE(frame.size == 0);
    REQUIRE(frame.data == nullptr);
}

TEST_CASE("getting csv frame") {
    std::cout << "initilized" << std::endl;
    //Arrange
    char content[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";

    //Act
    writer.write(content);
    Frame frame =  framer.get_frame();

    //Assert
    for (int i = 0; i < std::strlen(content) - 1; i++) {
        std::cout << frame.data[i];
        REQUIRE(content[i] == frame.data[i]);
    }
    std::cout << "\n";
    REQUIRE(frame.size == std::strlen(content) - 1);
}