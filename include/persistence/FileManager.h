#ifndef COSMO_SOFT_FILEMANAGER_H
#define COSMO_SOFT_FILEMANAGER_H
#include <cstdint>
#include <string>

//manages File I/O
//STICK TO STANDARD LIBRARY FUNCTIONS (keeps the code platform-agnostic)
class FileManager {
public:
    FileManager();

    bool open(const std::string& path);
    void close();
    [[nodiscard]] bool isOpen() const;

    ssize_t write(const uint8_t* data, size_t size);

    //ssize_t read(const uint8_t* buffer);

    //ssize_t readLine(const uint8_t* buffer, int line);

private:
    std::string path;
    std::string openFlag;

};

#endif //COSMO_SOFT_FILEMANAGER_H