#include "Hazard_LittleFS.h"
#ifdef ARDUINO
#include <LittleFS.h>
#include <cerrno>
#include <cstdio>
#include <unistd.h>

namespace
{
// Stdio uses the ESP32 VFS mounted by LittleFS.begin(). It exposes flush/close
// failures that Arduino File's void flush()/close() methods cannot report.
const char *slotPath(uint8_t slot)
{
    return slot == 0 ? "/littlefs/hazards-a.bin" : "/littlefs/hazards-b.bin";
}
}
bool HazardLittleFS::begin()
{
    mounted = LittleFS.begin(false, "/littlefs", 4); // Never format on mount failure.
    return mounted;
}
SlotRead HazardLittleFS::read(uint8_t slot, uint8_t *data, size_t capacity, size_t &size)
{
    size = 0;
    if (!mounted || slot > 1) return SlotRead::Error;
    errno = 0;
    FILE *file = std::fopen(slotPath(slot), "rb");
    if (!file) return errno == ENOENT ? SlotRead::Missing : SlotRead::Error;
    if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return SlotRead::Error; }
    const long length = std::ftell(file);
    if (length < 0) { std::fclose(file); return SlotRead::Error; }
    size = static_cast<size_t>(length);
    if (size > capacity) { std::fclose(file); return SlotRead::Invalid; }
    std::rewind(file);
    const bool ok = std::fread(data, 1, size, file) == size && !std::ferror(file);
    const int closed = std::fclose(file);
    return ok && !closed ? SlotRead::Ok : SlotRead::Error;
}
bool HazardLittleFS::write(uint8_t slot, const uint8_t *data, size_t size)
{
    if (!mounted || slot > 1) return false;
    FILE *file = std::fopen(slotPath(slot), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data, 1, size, file) == size &&
                    std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    const int closed = std::fclose(file);
    return ok && !closed;
}
#endif
