#pragma once
#include "Hazard_Storage.h"
#ifdef ARDUINO
class HazardLittleFS : public HazardStorageBackend
{
public:
    HazardLittleFS() : mounted(false) {}
    bool begin();
    SlotRead read(uint8_t slot, uint8_t *data, size_t capacity, size_t &size) override;
    bool write(uint8_t slot, const uint8_t *data, size_t size) override;
private:
    bool mounted;
};
#endif
