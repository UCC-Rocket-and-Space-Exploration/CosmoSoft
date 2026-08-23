
#include "services/telemetry/FileLogger.h"

#include <mutex>

void FileLogger::Log(std::string record) {
    std::lock_guard<std::mutex> lk(m_locker);
    m_file_stream << record;
}

void FileLogger::Log(unsigned char* record) {
    std::lock_guard<std::mutex> lk(m_locker);
    m_file_stream << record;
}

void FileLogger::LogLine(std::string record) {
    Log(record);
    m_file_stream << '\n';
}

void FileLogger::LogLine(unsigned char *record) {
    Log(record);
    m_file_stream << '\n';
}
