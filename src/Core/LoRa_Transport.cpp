#include "LoRa_Transport.h"
#include <LoRa.h>

namespace LoRaTransport
{
namespace
{
struct Subscriber { Handler handler; void *context; };
Subscriber subscribers[4] = {};
size_t subscriberCount = 0;
bool initialized = false;
bool transmitting = false;
}
bool addHandler(Handler handler, void *context)
{
    if (!handler || subscriberCount == 4) return false;
    subscribers[subscriberCount++] = Subscriber{handler, context};
    return true;
}
void begin()
{
    initialized = true;
    transmitting = false;
    LoRa.receive();
}
bool send(const uint8_t *data, size_t size)
{
    if (!initialized || !data || !size || size > 255 || transmitting) return false;
    if (!LoRa.beginPacket()) return false;
    if (LoRa.write(data, size) != size) { LoRa.receive(); return false; }
    transmitting = true;
    if (LoRa.endPacket(true) != 1) { transmitting = false; LoRa.receive(); return false; }
    return true;
}
void poll()
{
    if (!initialized) return;
    if (transmitting)
    {
        // Public beginPacket() returns zero while async TX is still running.
        // Once complete it enters standby; immediately restore receive mode.
        if (!LoRa.beginPacket()) return;
        transmitting = false;
        LoRa.receive();
    }
    const int length = LoRa.parsePacket();
    if (length <= 0) return;
    uint8_t data[255];
    size_t size = 0;
    while (LoRa.available() && size < sizeof(data)) data[size++] = static_cast<uint8_t>(LoRa.read());
    if (size == static_cast<size_t>(length))
        for (size_t i = 0; i < subscriberCount; ++i)
            if (subscribers[i].handler(data, size, subscribers[i].context)) break;
    if (!transmitting) LoRa.receive();
}
}
