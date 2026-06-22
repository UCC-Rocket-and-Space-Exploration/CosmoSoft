#include "gateway/comms/SerialFramerWorker.h"

#include "catch2/catch_test_macros.hpp"
#include "gateway/comms/windows/SerialCommsWindows.h"
#include "include/SerialWriter.h"
#include "services/RingBuffer.h"
#include "services/telemetry/CsvFramer.h"
#include "include/functions.h"



TEST_CASE("adds 2 csv frames") {
    std::shared_ptr<IComms> writer_comm = std::make_shared<SerialCommsWindows>("COM1");
    const std::shared_ptr<IComms> reader_comm = std::make_shared<SerialCommsWindows>("COM2");

    std::shared_ptr<CsvFramer> csv_framer = std::make_shared<CsvFramer>(reader_comm);
    SerialWriter writer(writer_comm);
    std::shared_ptr<RingBuffer<Frame>> buffer = std::make_shared<RingBuffer<Frame>>(10);
    SerialFramerWorker f_worker(csv_framer, buffer);

    char frame_content1[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";
    char frame_content2[] = "5555,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
    char frame_content3[] = "66667,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";

    writer.write(frame_content1);
    writer.write(frame_content2);
    writer.write(frame_content3);

    std::cout << "wrote" << std::endl;
    Sleep(1000);
    // Act
    f_worker.run();
    Sleep(1000);

    std::cout << "getting frames" << std::endl;
    buffer->show();

    auto opt_frame1 = buffer->get();
    auto opt_frame2 = buffer->get();
    auto opt_frame3 = buffer->get();

    // //Assert
    std::cout << "assert" << std::endl;

    REQUIRE(opt_frame1.has_value());
    REQUIRE(opt_frame2.has_value());
    // assert(opt_frame3.has_value());

    excepted_actual_content_equal_except_last_char(frame_content1, opt_frame1.value().data);
    excepted_actual_content_equal_except_last_char(frame_content2, opt_frame2.value().data);

    Sleep(100);
    f_worker.stop();
    std::cout << "after all" << std::endl;
}
