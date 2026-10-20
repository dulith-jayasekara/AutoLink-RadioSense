#pragma once
#include <stddef.h>
#include <stdint.h>

// One owner polls the radio and dispatches complete packets to registered modules.
namespace LoRaTransport
{
using Handler = bool (*)(const uint8_t *data, size_t size, void *context);
bool addHandler(Handler handler, void *context);
void begin(); // Call after LoRa_Initialization().
bool send(const uint8_t *data, size_t size);
void poll();
}
