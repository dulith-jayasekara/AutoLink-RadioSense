#include "Hazard_Storage.h"
#include <cstring>
#include <limits>

namespace
{
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Snapshot format requires IEEE-754 binary64");
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Snapshot format requires IEEE-754 binary32");
void put(uint8_t *&p, uint64_t value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) { *p++ = static_cast<uint8_t>(value); value >>= 8; }
}
uint64_t get(const uint8_t *&p, size_t bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= static_cast<uint64_t>(*p++) << (8 * i);
    return value;
}
void putDouble(uint8_t *&p, double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    put(p, bits, 8);
}
double getDouble(const uint8_t *&p)
{
    const uint64_t bits = get(p, 8);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}

uint32_t HazardSnapshot::checksum(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i)
    {
        // CRC field itself is treated as zero.
        crc ^= (i >= 20 && i < 24) ? 0 : data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

bool HazardSnapshot::encode(const HazardRepository &repo, uint64_t generation,
                            uint8_t *data, size_t capacity, size_t &size)
{
    size = 0;
    const size_t required = HEADER_SIZE + repo.count() * RECORD_SIZE +
        repo.observationCount() * HazardEvidenceCodec::SIZE +
        (repo.retirementCount() ? 2 + repo.retirementCount() * 13 : 0);
    if (!data || !generation || capacity < required) return false;
    uint8_t *p = data;
    put(p, 0x324d4841u, 4); // "AHM2"
    put(p, SCHEMA_VERSION, 2); // Schema 4 adds optional compact terminal markers; schemas 1-3 remain readable.
    put(p, RECORD_SIZE, 2);
    put(p, repo.count(), 2);
    put(p, repo.observationCount(), 2);
    put(p, generation, 8);
    put(p, 0, 4);
    for (size_t i = 0; i < repo.count(); ++i)
    {
        HazardRecord r;
        if (!repo.getAt(i, r) || !isValidHazardRecord(r)) return false;
        put(p, r.id, 8); put(p, r.originNodeId, 4); put(p, r.updatedByNodeId, 4);
        put(p, static_cast<uint8_t>(r.type), 1); put(p, r.severity, 1);
        putDouble(p, r.latitude); putDouble(p, r.longitude);
        put(p, r.createdAt, 4); put(p, r.lastObservedAt, 4); put(p, r.locationFixAt, 4);
        uint32_t accuracy;
        std::memcpy(&accuracy, &r.locationAccuracyMeters, sizeof(accuracy));
        put(p, accuracy, 4); put(p, r.expiresAt, 4); put(p, r.version, 4);
        put(p, r.confidence, 1); put(p, static_cast<uint8_t>(r.status), 1);
    }
    for (size_t i = 0; i < repo.observationCount(); ++i)
    {
        HazardObservation o;
        if (!repo.observationAt(i, o) || !HazardEvidenceCodec::encode(o, p)) return false;
        p += HazardEvidenceCodec::SIZE;
    }
    if (repo.retirementCount())
    {
        put(p, repo.retirementCount(), 2);
        for (size_t i = 0; i < repo.retirementCount(); ++i)
        {
            HazardRetirement marker; repo.retirementAt(i, marker);
            if (!validRetirement(marker)) return false;
            put(p, marker.id, 8); put(p, marker.createdAt, 4);
            put(p, static_cast<uint8_t>(marker.status), 1);
        }
    }
    uint8_t *crcField = data + 20;
    put(crcField, checksum(data, required), 4);
    size = required;
    return true;
}

bool HazardSnapshot::decode(const uint8_t *data, size_t size, HazardRepository &repo,
                            uint64_t &generation)
{
    if (!data || size < HEADER_SIZE || size > MAX_SIZE) return false;
    const uint8_t *p = data;
    if (get(p, 4) != 0x324d4841u) return false;
    const uint64_t schema = get(p, 2);
    if (schema < 1 || schema > SCHEMA_VERSION || get(p, 2) != RECORD_SIZE) return false;
    const size_t count = static_cast<size_t>(get(p, 2));
    const size_t observations = static_cast<size_t>(get(p, 2));
    if ((schema == 1 && observations) || observations > HazardRepository::EVIDENCE_CAPACITY ||
        count > HazardRepository::CAPACITY ||
        size < HEADER_SIZE + count * RECORD_SIZE + observations * HazardEvidenceCodec::SIZE) return false;
    const size_t baseSize = HEADER_SIZE + count * RECORD_SIZE + observations * HazardEvidenceCodec::SIZE;
    if (schema < 4 && size != baseSize) return false;
    const uint64_t decodedGeneration = get(p, 8);
    const uint32_t crc = static_cast<uint32_t>(get(p, 4));
    if (!decodedGeneration || crc != checksum(data, size)) return false;
    HazardRepository staged;
    for (size_t i = 0; i < count; ++i)
    {
        HazardRecord r = {};
        r.id = get(p, 8); r.originNodeId = static_cast<uint32_t>(get(p, 4));
        r.updatedByNodeId = static_cast<uint32_t>(get(p, 4));
        r.type = static_cast<HazardType>(get(p, 1)); r.severity = static_cast<uint8_t>(get(p, 1));
        r.latitude = getDouble(p); r.longitude = getDouble(p);
        r.createdAt = static_cast<uint32_t>(get(p, 4));
        r.lastObservedAt = static_cast<uint32_t>(get(p, 4));
        r.locationFixAt = static_cast<uint32_t>(get(p, 4));
        const uint32_t accuracy = static_cast<uint32_t>(get(p, 4));
        std::memcpy(&r.locationAccuracyMeters, &accuracy, sizeof(accuracy));
        r.expiresAt = static_cast<uint32_t>(get(p, 4));
        r.version = static_cast<uint32_t>(get(p, 4));
        r.confidence = static_cast<uint8_t>(get(p, 1));
        r.status = static_cast<HazardStatus>(get(p, 1));
        if (!staged.add(r)) return false; // Includes semantic validation and duplicate IDs.
    }
    for (size_t i = 0; i < observations; ++i)
    {
        HazardObservation o;
        if (!HazardEvidenceCodec::decode(p, o) ||
            (schema < 3 && o.kind > ObservationKind::Clearance) || !staged.addObservation(o)) return false;
        p += HazardEvidenceCodec::SIZE;
    }
    if (size != baseSize)
    {
        if (size < baseSize + 2) return false;
        const size_t markers = static_cast<size_t>(get(p, 2));
        if (!markers || markers > HazardRepository::RETIREMENT_CAPACITY || size != baseSize + 2 + markers * 13) return false;
        for (size_t i = 0; i < markers; ++i)
        {
            HazardRetirement marker;
            marker.id = get(p, 8); marker.createdAt = static_cast<uint32_t>(get(p, 4));
            marker.status = static_cast<HazardStatus>(get(p, 1));
            if (staged.findById(marker.id) || staged.findRetirement(marker.id)) return false;
            for (size_t j = 0; j < staged.observationCount(); ++j)
            { HazardObservation o; staged.observationAt(j, o); if (o.hazardId == marker.id) return false; }
            if (!staged.retire(marker)) return false;
        }
    }
    repo = staged;
    generation = decodedGeneration;
    return true;
}

const char *storageLoadName(StorageLoad result)
{
    switch (result)
    {
    case StorageLoad::Empty: return "EMPTY";
    case StorageLoad::Loaded: return "LOADED";
    case StorageLoad::Recovered: return "RECOVERED_PREVIOUS_VALID_SNAPSHOT";
    case StorageLoad::Corrupt: return "CORRUPT_READ_ONLY";
    case StorageLoad::IoError: return "IO_ERROR_READ_ONLY";
    }
    return "UNKNOWN";
}

HazardStorage::HazardStorage(HazardStorageBackend &value)
    : backend(value), currentGeneration(0), activeSlot(-1), canWrite(false) {}

StorageLoad HazardStorage::load(HazardRepository &repo)
{
    canWrite = false;
    currentGeneration = 0;
    activeSlot = -1;
    bool invalid = false;
    uint32_t chosenCrc = 0;
    HazardRepository best;
    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        size_t size = 0;
        const SlotRead read = backend.read(slot, buffer, sizeof(buffer), size);
        if (read == SlotRead::Error) return StorageLoad::IoError;
        if (read == SlotRead::Missing) continue;
        HazardRepository candidate;
        uint64_t generation = 0;
        if (read == SlotRead::Invalid || !HazardSnapshot::decode(buffer, size, candidate, generation))
        { invalid = true; continue; }
        const uint32_t crc = HazardSnapshot::checksum(buffer, size);
        // Equal generations with different contents have no unambiguous winner.
        if (activeSlot >= 0 && generation == currentGeneration && crc != chosenCrc)
            return StorageLoad::Corrupt;
        if (activeSlot < 0 || generation > currentGeneration)
        {
            best = candidate;
            activeSlot = slot;
            currentGeneration = generation;
            chosenCrc = crc;
        }
    }
    if (activeSlot < 0 && invalid) return StorageLoad::Corrupt;
    repo = best;
    canWrite = true;
    if (activeSlot < 0) return StorageLoad::Empty;
    return invalid ? StorageLoad::Recovered : StorageLoad::Loaded;
}

bool HazardStorage::save(const HazardRepository &repo)
{
    if (!canWrite) return false;
    if (currentGeneration == UINT64_MAX) { canWrite = false; return false; }
    size_t size;
    const uint64_t next = currentGeneration + 1;
    if (!HazardSnapshot::encode(repo, next, buffer, sizeof(buffer), size)) return false;
    const uint32_t expectedCrc = HazardSnapshot::checksum(buffer, size);
    const uint8_t target = activeSlot == 0 ? 1 : 0;
    // A failed acknowledgement can leave a complete new snapshot on disk.
    // Freeze writes until recovery selects the actual on-disk generation.
    canWrite = false;
    if (!backend.write(target, buffer, size)) return false;
    size_t actual = 0;
    if (backend.read(target, buffer, sizeof(buffer), actual) != SlotRead::Ok) return false;
    HazardRepository verified;
    uint64_t generation = 0;
    if (!HazardSnapshot::decode(buffer, actual, verified, generation) || generation != next ||
        actual != size || HazardSnapshot::checksum(buffer, actual) != expectedCrc) return false;
    currentGeneration = next;
    activeSlot = target;
    canWrite = true;
    return true;
}
