#include "Hazard_Storage.h"
#include "Hazard_Service.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

namespace
{
constexpr uint32_t NOW = 1792454400;
HazardRecord record(uint32_t sequence)
{
    HazardRecord r = {};
    r.id = (static_cast<HazardId>(7) << 32) | sequence;
    r.originNodeId = 7; r.updatedByNodeId = 8;
    r.type = HazardType::Flooding; r.severity = 4;
    r.latitude = 6.9271; r.longitude = 79.8612;
    r.createdAt = NOW; r.lastObservedAt = NOW + 10;
    r.locationFixAt = NOW + 8; r.locationAccuracyMeters = 3.5f;
    r.expiresAt = NOW + 7210; r.version = 9; r.confidence = 75;
    r.status = HazardStatus::Stale;
    return r;
}
std::vector<uint8_t> encode(const HazardRepository &repo, uint64_t gen)
{
    std::vector<uint8_t> data(HazardSnapshot::MAX_SIZE);
    size_t size;
    assert(HazardSnapshot::encode(repo, gen, data.data(), data.size(), size));
    data.resize(size);
    return data;
}
void crc(std::vector<uint8_t> &data)
{
    const uint32_t value = HazardSnapshot::checksum(data.data(), data.size());
    for (unsigned i = 0; i < 4; ++i) data[20 + i] = static_cast<uint8_t>(value >> (8 * i));
}
class MemoryBackend : public HazardStorageBackend
{
public:
    std::vector<uint8_t> slots[2];
    bool exists[2] = {false, false};
    int errorSlot = -1;
    size_t cut = static_cast<size_t>(-1);
    bool loseAcknowledgement = false;
    bool breakReadback = false;
    size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *out, size_t capacity, size_t &size) override
    {
        size = 0;
        if (errorSlot == slot) return SlotRead::Error;
        if (!exists[slot]) return SlotRead::Missing;
        size = slots[slot].size();
        if (size > capacity) return SlotRead::Invalid;
        std::copy(slots[slot].begin(), slots[slot].end(), out);
        return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    {
        ++writes;
        const size_t actual = std::min(size, cut);
        exists[slot] = true;
        slots[slot].assign(data, data + actual);
        if (breakReadback) errorSlot = slot;
        return actual == size && !loseAcknowledgement;
    }
};

void testCodec()
{
    HazardRepository original;
    for (uint32_t i = 1; i <= HazardRepository::CAPACITY; ++i) assert(original.add(record(i)));
    const auto bytes = encode(original, 42);
    assert(bytes.size() == HazardSnapshot::HEADER_SIZE + 16 * HazardSnapshot::RECORD_SIZE);
    assert(bytes[0] == 'A' && bytes[1] == 'H' && bytes[2] == 'M' && bytes[3] == '2');
    assert(bytes[4] == 2 && bytes[6] == 60 && bytes[8] == 16 && bytes[12] == 42);
    // Known offsets guard the explicit wire layout, including little-endian node identity.
    assert(bytes[24] == 1 && bytes[28] == 7 && bytes[32] == 7 && bytes[36] == 8);
    assert(bytes[40] == 3 && bytes[41] == 4 && bytes[82] == 75 && bytes[83] == 4);
    HazardRepository restored;
    uint64_t generation = 0;
    assert(HazardSnapshot::decode(bytes.data(), bytes.size(), restored, generation));
    assert(generation == 42 && encode(restored, generation) == bytes);
    const HazardRecord *r = restored.findById(record(1).id);
    assert(r && r->latitude == 6.9271 && r->longitude == 79.8612 && r->locationAccuracyMeters == 3.5f);
    // Corruption and truncation must never partially replace a destination repository.
    HazardRepository untouched;
    assert(untouched.add(record(99)));
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        auto damaged = bytes;
        damaged[i] ^= 1;
        generation = 123;
        assert(!HazardSnapshot::decode(damaged.data(), damaged.size(), untouched, generation));
        assert(untouched.count() == 1 && untouched.findById(record(99).id) && generation == 123);
        assert(!HazardSnapshot::decode(bytes.data(), i, untouched, generation));
    }
    auto bad = bytes;
    bad[40] = 255; crc(bad); // Semantically invalid type with a valid CRC.
    assert(!HazardSnapshot::decode(bad.data(), bad.size(), untouched, generation));
    bad = bytes;
    std::copy(bad.begin() + 24, bad.begin() + 84, bad.begin() + 84); crc(bad);
    assert(!HazardSnapshot::decode(bad.data(), bad.size(), untouched, generation)); // Duplicate ID.
    bad = bytes; bad[4] = 3; crc(bad);
    assert(!HazardSnapshot::decode(bad.data(), bad.size(), untouched, generation));
    bad = bytes; bad[8] = 17; crc(bad);
    assert(!HazardSnapshot::decode(bad.data(), bad.size(), untouched, generation));
    bad = bytes; bad.push_back(0);
    assert(!HazardSnapshot::decode(bad.data(), bad.size(), untouched, generation));
    uint8_t tiny[1]; size_t size = 99;
    assert(!HazardSnapshot::encode(original, 1, tiny, sizeof(tiny), size) && size == 0);
    HazardRepository empty;
    const auto emptyBytes = encode(empty, 1);
    assert(HazardSnapshot::decode(emptyBytes.data(), emptyBytes.size(), restored, generation));
    assert(restored.count() == 0);
    std::cout << "PASS: snapshot round-trip, all-byte corruption/truncation, schema and semantic validation.\n";
}

void testRecovery()
{
    MemoryBackend backend;
    HazardRepository repo;
    HazardStorage storage(backend);
    assert(!storage.save(repo)); // Must load before saving.
    assert(storage.load(repo) == StorageLoad::Empty);
    assert(repo.add(record(1)) && storage.save(repo));
    assert(storage.generation() == 1);
    const auto first = backend.slots[0];
    assert(repo.add(record(2)) && storage.save(repo));
    assert(backend.slots[0] == first && storage.generation() == 2);
    HazardRepository restored;
    HazardStorage restarted(backend);
    assert(restarted.load(restored) == StorageLoad::Loaded && restored.count() == 2);
    backend.slots[1][25] ^= 1;
    HazardStorage recovered(backend);
    assert(recovered.load(restored) == StorageLoad::Recovered && restored.count() == 1);
    assert(recovered.save(restored) && recovered.generation() == 2);
    backend.slots[0][0] ^= 1; backend.slots[1][0] ^= 1;
    HazardStorage damaged(backend);
    assert(damaged.load(restored) == StorageLoad::Corrupt && !damaged.writable());
    assert(restored.count() == 1 && !damaged.save(restored));
    backend = MemoryBackend{}; backend.errorSlot = 1;
    HazardStorage io(backend);
    assert(io.load(restored) == StorageLoad::IoError && !io.writable());
    assert(restored.count() == 1);
    backend = MemoryBackend{};
    HazardRepository other; assert(other.add(record(2)));
    backend.exists[0] = backend.exists[1] = true;
    backend.slots[0] = encode(restored, 3); backend.slots[1] = encode(other, 3);
    HazardStorage ambiguous(backend);
    assert(ambiguous.load(restored) == StorageLoad::Corrupt && !ambiguous.writable());
    backend = MemoryBackend{};
    backend.exists[0] = true; backend.slots[0] = encode(other, UINT64_MAX);
    HazardStorage exhausted(backend);
    assert(exhausted.load(restored) == StorageLoad::Loaded && !exhausted.save(restored));
    assert(!exhausted.writable());
    std::cout << "PASS: newest snapshot recovery, corruption fallback and read-only error handling.\n";
}

void testEveryInterruptedWrite()
{
    MemoryBackend baseline;
    HazardRepository old;
    assert(old.add(record(1)));
    HazardStorage initial(baseline);
    HazardRepository unused;
    assert(initial.load(unused) == StorageLoad::Empty && initial.save(old));
    HazardRepository updated = old;
    assert(updated.add(record(2)));
    const size_t snapshotSize = encode(updated, 2).size();
    for (size_t cut = 0; cut < snapshotSize; ++cut)
    {
        MemoryBackend backend = baseline;
        HazardStorage writer(backend);
        HazardRepository live;
        assert(writer.load(live) == StorageLoad::Loaded);
        backend.cut = cut;
        assert(!writer.save(updated) && !writer.writable());
        const size_t writes = backend.writes;
        assert(!writer.save(updated) && backend.writes == writes);
        HazardStorage reboot(backend);
        HazardRepository restored;
        assert(reboot.load(restored) == StorageLoad::Recovered);
        assert(restored.count() == 1 && restored.findById(record(1).id));
    }
    MemoryBackend lostAck = baseline;
    HazardStorage writer(lostAck);
    assert(writer.load(unused) == StorageLoad::Loaded);
    lostAck.loseAcknowledgement = true;
    assert(!writer.save(updated) && !writer.writable());
    HazardStorage reboot(lostAck);
    assert(reboot.load(unused) == StorageLoad::Loaded && unused.count() == 2);
    MemoryBackend readError = baseline;
    HazardStorage reader(readError);
    assert(reader.load(unused) == StorageLoad::Loaded);
    readError.breakReadback = true;
    assert(!reader.save(updated) && !reader.writable());
    MemoryBackend firstWrite;
    HazardStorage fresh(firstWrite);
    HazardRepository freshRepo;
    assert(fresh.load(freshRepo) == StorageLoad::Empty);
    firstWrite.cut = 8;
    assert(!fresh.save(updated));
    HazardStorage failedFirstBoot(firstWrite);
    assert(failedFirstBoot.load(freshRepo) == StorageLoad::Corrupt);
    assert(!failedFirstBoot.writable() && freshRepo.count() == 0);
    std::cout << "PASS: interrupted writes at all " << snapshotSize
              << " byte boundaries retain previous records; uncertain commits freeze writes.\n";
}

struct Sequence
{
    uint32_t value = 0;
    static bool reserve(uint32_t next, void *context)
    {
        Sequence &sequence = *static_cast<Sequence *>(context);
        if (next <= sequence.value) return false;
        sequence.value = next;
        return true;
    }
};
void testServiceTransactions()
{
    MemoryBackend backend;
    HazardStorage storage(backend);
    HazardRepository repo;
    assert(storage.load(repo) == StorageLoad::Empty);
    Sequence sequence;
    HazardService service(repo, 7, 0, Sequence::reserve, &sequence, &storage);
    PhoneHazardReport input{HazardType::Flooding, 3, 6.9271, 79.8612, NOW, NOW, 5};
    HazardId id;
    assert(service.create(input, NOW, id) == HazardResult::Ok);
    const HazardId originalId = id;
    backend.cut = 10;
    input.observedAt++; input.locationFixAt++; input.severity = 5;
    assert(service.revise(id, input, NOW + 1) == HazardResult::RecordPersistenceFailed);
    assert(repo.findById(id)->version == 1 && repo.findById(id)->severity == 3);
    assert(service.create(input, NOW + 1, id) == HazardResult::RecordPersistenceFailed && id == 0);
    assert(sequence.value == 2); // Frozen storage does not consume IDs.
    backend.cut = static_cast<size_t>(-1);
    assert(storage.load(repo) == StorageLoad::Recovered);
    assert(service.revise(originalId, input, NOW + 1) == HazardResult::Ok);
    HazardStorage restarted(backend);
    HazardRepository restored;
    assert(restarted.load(restored) == StorageLoad::Loaded);
    assert(restored.findById(originalId)->version == 2);
    // A failed create reserves an ID but never exposes an unpersisted record in RAM.
    backend.cut = 1;
    assert(service.create(input, NOW + 1, id) == HazardResult::RecordPersistenceFailed);
    assert(id == 0 && sequence.value == 4 && repo.count() == 1);
    backend.cut = static_cast<size_t>(-1);
    assert(restarted.load(restored) == StorageLoad::Recovered);
    HazardService afterReboot(restored, 7, sequence.value, Sequence::reserve, &sequence, &restarted);
    assert(afterReboot.create(input, NOW + 1, id) == HazardResult::Ok);
    assert(static_cast<uint32_t>(id) == 5);
    std::cout << "PASS: create/revise acknowledgement follows verified persistence; failed IDs are not reused.\n";
}

// Real host files exercise the same storage manager/codec, not the ESP32 LittleFS driver.
class FileBackend : public HazardStorageBackend
{
public:
    explicit FileBackend(const std::string &directory) : directory(directory) {}
    std::string path(uint8_t slot) { return directory + (slot ? "/hazards-b.bin" : "/hazards-a.bin"); }
    SlotRead read(uint8_t slot, uint8_t *out, size_t capacity, size_t &size) override
    {
        size = 0;
        errno = 0;
        FILE *file = std::fopen(path(slot).c_str(), "rb");
        if (!file) return errno == ENOENT ? SlotRead::Missing : SlotRead::Error;
        if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return SlotRead::Error; }
        const long length = std::ftell(file);
        if (length < 0) { std::fclose(file); return SlotRead::Error; }
        size = static_cast<size_t>(length);
        if (size > capacity) { std::fclose(file); return SlotRead::Invalid; }
        std::rewind(file);
        const bool ok = std::fread(out, 1, size, file) == size && !std::ferror(file);
        const int closed = std::fclose(file);
        return ok && !closed ? SlotRead::Ok : SlotRead::Error;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    {
        FILE *file = std::fopen(path(slot).c_str(), "wb");
        if (!file) return false;
        const bool ok = std::fwrite(data, 1, size, file) == size &&
                        std::fflush(file) == 0 && fsync(fileno(file)) == 0;
        const int closed = std::fclose(file);
        return ok && !closed;
    }
private:
    std::string directory;
};
void testRealFiles(const char *directory)
{
    FileBackend files(directory);
    {
        HazardStorage before(files);
        HazardRepository repo;
        assert(before.load(repo) == StorageLoad::Empty);
        assert(repo.add(record(1)) && before.save(repo));
        assert(repo.add(record(2)) && before.save(repo));
    }
    // Destroy all storage/repository instances, then read only the files left behind.
    HazardStorage reboot(files);
    HazardRepository repo;
    assert(reboot.load(repo) == StorageLoad::Loaded && repo.count() == 2);
    assert(repo.findById(record(1).id)->lastObservedAt == NOW + 10);
    FILE *file = std::fopen(files.path(1).c_str(), "wb");
    assert(file && std::fwrite("partial", 1, 7, file) == 7);
    assert(std::fclose(file) == 0);
    HazardStorage recovered(files);
    assert(recovered.load(repo) == StorageLoad::Recovered && repo.count() == 1);
    std::cout << "PASS: real host files survive instance restart; truncated newest file restores prior snapshot.\n";
}
}

void testHazardStorage(const char *directory)
{
    testCodec(); testRecovery(); testEveryInterruptedWrite();
    testServiceTransactions(); testRealFiles(directory);
}
