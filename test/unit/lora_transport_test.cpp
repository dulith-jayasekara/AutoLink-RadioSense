#include "LoRa.h"
#include "LoRa_Transport.h"
#include <cassert>
#include <iostream>

LoRaClass LoRa;
namespace
{
int firstCalls = 0, secondCalls = 0, thirdCalls = 0;
bool reply = false;
bool first(const uint8_t *, size_t, void *) { ++firstCalls; return false; }
bool second(const uint8_t *data, size_t size, void *)
{
    ++secondCalls;
    assert(size == 3 && data[0] == 1 && data[2] == 3);
    if (reply) assert(LoRaTransport::send(data, size));
    return true;
}
bool third(const uint8_t *, size_t, void *) { ++thirdCalls; return true; }
}
int main()
{
    const uint8_t packet[] = {1, 2, 3};
    assert(!LoRaTransport::send(packet, sizeof(packet)));
    assert(!LoRaTransport::addHandler(nullptr, nullptr));
    assert(LoRaTransport::addHandler(first, nullptr));
    assert(LoRaTransport::addHandler(second, nullptr));
    assert(LoRaTransport::addHandler(third, nullptr));
    assert(LoRaTransport::addHandler(third, nullptr));
    assert(!LoRaTransport::addHandler(third, nullptr));
    LoRaTransport::begin();
    assert(LoRa.receiving);
    assert(!LoRaTransport::send(nullptr, 1) && !LoRaTransport::send(packet, 0));
    assert(!LoRaTransport::send(packet, 256));
    assert(LoRaTransport::send(packet, sizeof(packet)));
    assert(LoRa.busy && !LoRa.receiving && LoRa.written == 3);
    assert(!LoRaTransport::send(packet, sizeof(packet)));
    LoRaTransport::poll(); assert(!LoRa.receiving);
    LoRa.busy = false; // Fake completion of asynchronous hardware transmission.
    LoRaTransport::poll(); assert(LoRa.receiving);
    LoRa.shortWrite = true;
    assert(!LoRaTransport::send(packet, sizeof(packet)) && LoRa.receiving);
    LoRa.shortWrite = false;
    LoRa.inject(packet, sizeof(packet));
    LoRaTransport::poll();
    assert(firstCalls == 1 && secondCalls == 1 && thirdCalls == 0);
    reply = true; LoRa.inject(packet, sizeof(packet)); LoRaTransport::poll();
    assert(LoRa.busy && !LoRa.receiving); // Dispatch must not cancel a subscriber's TX.
    LoRa.busy = false; LoRaTransport::poll(); assert(LoRa.receiving);
    std::cout << "PASS: radio dispatcher routing, bounded handlers, async busy/completion and short-write recovery (fake radio).\n";
}
