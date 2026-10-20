#pragma once
#include "Hazard_Repository.h"

// Backends must distinguish missing files from unreadable files.
enum class SlotRead { Ok, Missing, Invalid, Error };
class HazardStorageBackend
{
public:
    virtual ~HazardStorageBackend() {}
    virtual SlotRead read(uint8_t slot, uint8_t *data, size_t capacity, size_t &size) = 0;
    virtual bool write(uint8_t slot, const uint8_t *data, size_t size) = 0;
};

class HazardSnapshot
{
public:
    static constexpr uint16_t SCHEMA_VERSION = 4;
    static constexpr size_t HEADER_SIZE = 24;
    static constexpr size_t RECORD_SIZE = 60;
    static constexpr size_t MAX_SIZE = HEADER_SIZE + HazardRepository::CAPACITY * RECORD_SIZE +
        HazardRepository::EVIDENCE_CAPACITY * HazardEvidenceCodec::SIZE +
        2 + HazardRepository::RETIREMENT_CAPACITY * 13;
    static bool encode(const HazardRepository &repository, uint64_t generation,
                       uint8_t *data, size_t capacity, size_t &size);
    // On any failure the destination repository and generation remain unchanged.
    static bool decode(const uint8_t *data, size_t size, HazardRepository &repository,
                       uint64_t &generation);
    static uint32_t checksum(const uint8_t *data, size_t size);
};

enum class StorageLoad { Empty, Loaded, Recovered, Corrupt, IoError };
const char *storageLoadName(StorageLoad result);

class HazardStorage
{
public:
    explicit HazardStorage(HazardStorageBackend &backend);
    StorageLoad load(HazardRepository &repository);
    // Writes only the inactive slot, then reads it back and verifies it.
    // Failed writes disable further saves until load()/restart resolves disk state.
    bool save(const HazardRepository &repository);
    bool writable() const { return canWrite; }
    uint64_t generation() const { return currentGeneration; }
private:
    HazardStorageBackend &backend;
    uint8_t buffer[HazardSnapshot::MAX_SIZE];
    uint64_t currentGeneration;
    int activeSlot;
    bool canWrite;
};
