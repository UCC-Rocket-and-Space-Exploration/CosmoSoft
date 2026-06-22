//
// #include <cassert>
//
// #include "gateway/comms/SerialFramerWorker.h"
// #include "gateway/comms/windows/SerialCommsWindows.h"
// #include "include/SerialWriter.h"
// #include "services/RingBuffer.h"
// #include "services/telemetry/CsvFramer.h"
//
// int main() {
//     //Arrange
//
//     std::unique_ptr<SerialCommsWindows> writer_comm = std::make_unique<SerialCommsWindows>("COM1");
//     std::unique_ptr<SerialCommsWindows> reader_comm = std::make_unique<SerialCommsWindows>("COM2");
//
//     std::unique_ptr<CsvFramer> csv_framer = std::make_unique<CsvFramer>(std::move(reader_comm));
//     SerialWriter writer(std::move(writer_comm));
//     std::shared_ptr<RingBuffer<Frame>> buffer = std::make_shared<RingBuffer<Frame>>(5);
//     SerialFramerWorker f_worker(std::move(csv_framer), buffer);
//
//     f_worker.run();
//
//     char frame_content1[] = "13603,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,0.00\n";
//     char frame_content2[] = "5555,19.24,100896.15,165.59,0.704,-0.927,9.850,-0.047,0.091,0.091,-4.822,-2.985,16.506,0.000000,0.000000,5.00\n";
//
//     std::cout << "write frame 1:" << std::endl;
//     writer.write(frame_content1);
//     std::cout << "write frame 2: " << std::endl;
//     writer.write(frame_content2);
//     std::cout << std::endl;
//
//     // Act
//     f_worker.stop();
//
//     std::cout << "getting frames" << std::endl;
//
//     auto opt_frame1 = buffer->get();
//     auto opt_frame2 = buffer->get();
//
//     //Assert
//     std::cout << "assert" << std::endl;
//
//     // REQUIRE(opt_frame1.has_value());
//     assert(opt_frame2.has_value());
//
//     // auto frame1_data = opt_frame1.value();
//     // auto frame2_data = opt_frame2.value();
//     //
//     // excepted_actual_content_equal_except_last_char(frame_content1, frame1_data.data);
//     // excepted_actual_content_equal_except_last_char(frame_content2, frame2_data.data);
//     //
//     // std::cout << "\n";
//     //
// }
