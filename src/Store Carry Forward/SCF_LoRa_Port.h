#pragma once

#include "SCF_Radio_Adapter.h"

namespace scf
{
class LoRaPort final : public RadioPort
{
public:
    LoRaPort();

    bool initialize() override;
    PortReceiveResult receive(uint8_t *frame, size_t capacity,
                              size_t &length) override;
    bool transmit(const uint8_t *frame, size_t length) override;

private:
    bool ready;
};
}
