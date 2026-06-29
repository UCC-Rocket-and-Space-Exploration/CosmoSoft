#ifndef COSMO_SOFT_CSVFRAMER_H
#define COSMO_SOFT_CSVFRAMER_H
#include <memory>
#include <vector>

#include "gateway/comms/interfaces/IComms.h"
#include "services/interfaces/IFramer.h"

class CsvFramer final : public IFramer{
public:
    ~CsvFramer() override;
    explicit CsvFramer(const std::shared_ptr<IComms>& comms);
    Frame get_frame(const bool& running) override;

private:
    const char frame_separator = '\n';
    const std::shared_ptr<IComms>& m_comms;
};

#endif //COSMO_SOFT_CSVFRAMER_H