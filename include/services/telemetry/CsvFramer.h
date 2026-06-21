#ifndef COSMO_SOFT_CSVFRAMER_H
#define COSMO_SOFT_CSVFRAMER_H
#include <memory>
#include <utility>
#include <vector>

#include "gateway/comms/interfaces/IComms.h"
#include "services/interfaces/IFramer.h"

class CsvFramer : public IFramer{
public:
    ~CsvFramer() override;
    explicit CsvFramer(std::unique_ptr<IComms> comms);
    Frame get_frame() override;

private:
    std::unique_ptr<IComms> m_comms;
    std::vector<uint8_t> per_frame_buffer;
};
#endif //COSMO_SOFT_CSVFRAMER_H