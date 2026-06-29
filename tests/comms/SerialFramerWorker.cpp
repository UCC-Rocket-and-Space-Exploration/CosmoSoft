// #include "gateway/comms/SerialFramerWorker.h"
//
// #include "catch2/catch_test_macros.hpp"
// #include "gateway/comms/windows/SerialCommsWindows.h"
// #include "include/SerialWriter.h"
// #include "services/RingBuffer.h"
// #include "include/helpers.h"
// #include "services/telemetry/framers/CsvFramer.h"
//
//
// TEST_CASE("adds 2 csv frames") {
//     const std::shared_ptr<IComms> writer_comm = std::make_shared<SerialCommsWindows>("COM1");
//     const std::shared_ptr<IComms> reader_comm = std::make_shared<SerialCommsWindows>("COM2");
//     auto status = writer_comm->open();
//     REQUIRE(status == true);
//     std::shared_ptr<CsvFramer> csv_framer = std::make_shared<CsvFramer>(reader_comm);
//     SerialWriter writer(writer_comm);
//     std::shared_ptr<RingBuffer<Frame>> buffer = std::make_shared<RingBuffer<Frame>>(10);
//     SerialFramerWorker f_worker(csv_framer, buffer);
//
//     char frame_content1[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";
//     char frame_content2[] = "5555,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
//     char frame_content3[] = "66667,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
//
//     writer.write(frame_content1);
//     writer.write(frame_content2);
//     writer.write(frame_content3);
//
//     std::cout << "wrote" << std::endl;
//     // Act
//     f_worker.start();
//     Sleep(1000); // is needed to ensure frames are written(simulated), so worker is able to read them
//
//     std::cout << "getting frames" << std::endl;
//
//     auto opt_frame1 = buffer->get();
//     auto opt_frame2 = buffer->get();
//     auto opt_frame3 = buffer->get();
//
//     // //Assert
//     std::cout << "assert" << std::endl;
//
//     REQUIRE(opt_frame1.has_value());
//     REQUIRE(opt_frame2.has_value());
//     // assert(opt_frame3.has_value());
//
//     excepted_actual_content_equal_except_last_char(frame_content1, opt_frame1.value().data);
//     std::cout << std::endl;
//     excepted_actual_content_equal_except_last_char(frame_content2, opt_frame2.value().data);
//     std::cout << std::endl;
//     excepted_actual_content_equal_except_last_char(frame_content3, opt_frame3.value().data);
//     std::cout << std::endl;
//
//     f_worker.stop();
//     std::cout << "after all" << std::endl;
// }
