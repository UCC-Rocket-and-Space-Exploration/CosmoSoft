#ifndef COSMO_SOFT_ICOMMS_H
#define COSMO_SOFT_ICOMMS_H
#include <cstdint>
#include <memory>
#include <sys/types.h>

// Platform-agnostic comms interface
class IComms {
public:
    virtual ~IComms() = default;

    virtual bool open() = 0;            //open the connection
    virtual void close() = 0;           //close the connection
    virtual bool isOpen() const = 0;    //return if connection is open

    //Blocking write function
    // uint8_t* data: pointer to the start of a block of unsigned 8-bit integers (serial port streams operate on a per-byte basis)
    // size_t sze: how many bytes to write starting from the pointer as an unsigned long
    // returns a signed long of the amount of data written
    virtual ssize_t write(const uint8_t* data, size_t size) = 0;

    //Non-blocking read function
    // uint8_t* buffer: pointer to the start of a block of unsigned 8-bit integers to write to
    // size_t maxSize: how many bytes past the pointer to write to, i.e. the size of the buffer
    virtual ssize_t read(uint8_t* buffer, size_t maxSize) = 0;

    // Device info & metadata, add more later depending on applicability
    [[nodiscard]] virtual std::string getDeviceName() const = 0;
    [[nodiscard]] virtual std::string getDevicePort() const = 0;
};

#endif //COSMO_SOFT_ICOMMS_H