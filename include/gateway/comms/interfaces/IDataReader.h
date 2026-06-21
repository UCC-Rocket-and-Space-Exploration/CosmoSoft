#ifndef COSMO_SOFT_IDATAREADER_H
#define COSMO_SOFT_IDATAREADER_H
#include <cstdint>

class IDataReader {
public:
    virtual ~IDataReader() = default;
    virtual std::uint8_t* read(size_t size) = 0;
};
#endif //COSMO_SOFT_IDATAREADER_H