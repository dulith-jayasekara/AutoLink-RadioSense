#include "SCF_LoRa_Port.h"

#include "../../include/Library_Headers.h"

namespace scf
{
LoRaPort::LoRaPort()
    : ready(false)
{
}

bool LoRaPort::initialize()
{
    // LoRa_Manager must initialize the shared radio before this adapter is used.
    ready = true;
    return true;
}

PortReceiveResult LoRaPort::receive(uint8_t *frame, size_t capacity,
                                    size_t &length)
{
    length = 0;
    if (!ready || frame == nullptr || capacity == 0)
    {
        return PortReceiveResult::Error;
    }

    const int packetSize = LoRa.parsePacket();
    if (packetSize <= 0)
    {
        return PortReceiveResult::NoPacket;
    }
    if (static_cast<size_t>(packetSize) > capacity)
    {
        while (LoRa.available())
        {
            LoRa.read();
        }
        return PortReceiveResult::Overflow;
    }

    while (LoRa.available() && length < capacity)
    {
        const int value = LoRa.read();
        if (value < 0)
        {
            return PortReceiveResult::Error;
        }
        frame[length++] = static_cast<uint8_t>(value);
    }

    return length == static_cast<size_t>(packetSize)
               ? PortReceiveResult::Packet
               : PortReceiveResult::Error;
}

bool LoRaPort::transmit(const uint8_t *frame, size_t length)
{
    if (!ready || frame == nullptr || length == 0 ||
        length > RADIO_FRAME_CAPACITY)
    {
        return false;
    }
    if (LoRa.beginPacket() == 0 || LoRa.write(frame, length) != length)
    {
        return false;
    }

    return LoRa.endPacket(true) == 1;
}
}
