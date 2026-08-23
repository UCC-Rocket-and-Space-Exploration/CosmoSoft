
#ifndef COSMO_SOFT_FILELOGGER_H
#define COSMO_SOFT_FILELOGGER_H
#include <fstream>
#include <mutex>

#include "services/interfaces/ILogger.h"

class FileLogger : public ILogger{
public:
    FileLogger(const std::string& file_path) {
        m_file_stream.open(file_path);
    }
    void Log(std::string record) override;
    void Log(unsigned char* record) override;
    void LogLine(std::string record) override;
    void LogLine(unsigned char* record) override;

    ~FileLogger() override {
        m_file_stream.close();
    }
private:
    std::ofstream m_file_stream;
    std::mutex m_locker;
};


#endif //COSMO_SOFT_FILELOGGER_H