
#include "services/telemetry/FileLogger.h"

void FileLogger::Log(std::string record) {
    m_file_stream << record;
}

void FileLogger::Log(unsigned char* record) {
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
