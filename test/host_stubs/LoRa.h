#pragma once
#include <stddef.h>
#include <stdint.h>
#include <cstring>

// Behavioral fake for the public API used by LoRa_Transport, not a radio emulator.
class LoRaClass
{
public:
    bool busy = false, receiving = false, shortWrite = false;
    bool pending = false;
    uint8_t incoming[255] = {};
    size_t incomingSize = 0, readIndex = 0, written = 0;
    int beginPacket()
    {
        if (busy) return 0;
        receiving = false; written = 0; return 1;
    }
    size_t write(const uint8_t *, size_t size)
    {
        written = shortWrite ? size - 1 : size; return written;
    }
    int endPacket(bool async)
    {
        if (!async) return 0;
        busy = true; return 1;
    }
    void receive() { receiving = true; }
    int parsePacket()
    {
        if (!receiving || !pending) return 0;
        pending = false; readIndex = 0; return static_cast<int>(incomingSize);
    }
    int available() { return static_cast<int>(incomingSize - readIndex); }
    int read() { return incoming[readIndex++]; }
    void inject(const uint8_t *data, size_t size)
    {
        std::memcpy(incoming, data, size); incomingSize = size; pending = true;
    }
};
extern LoRaClass LoRa;
