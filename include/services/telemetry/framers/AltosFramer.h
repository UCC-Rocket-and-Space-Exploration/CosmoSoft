//
// Created by Lenovo on 6/28/2026.
//

#ifndef COSMO_SOFT_TELEMEGAFRAMER_H
#define COSMO_SOFT_TELEMEGAFRAMER_H
#include <memory>

#include "gateway/comms/interfaces/IComms.h"
#include "services/interfaces/IFramer.h"

class AltosFramer final : public IFramer {
public:
    ~AltosFramer() override;
    explicit AltosFramer(const std::shared_ptr<IComms>& comms) : m_comms(comms) {}
    Frame get_frame(const bool& running) override;

    static bool sign_start(uint8_t byte);

private:
    std::shared_ptr<IComms> m_comms;
    const std::string m_frames_separator = "TELEM";
    // const uint8_t packet_length_value = 0x22;
    void verify_sign(bool running) const;
};
#endif //COSMO_SOFT_TELEMEGAFRAMER_H