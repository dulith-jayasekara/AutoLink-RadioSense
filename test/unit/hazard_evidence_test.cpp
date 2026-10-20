#include "Hazard_Groups.h"
#include "Hazard_Console.h"
#include "Hazard_Storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
constexpr uint32_t NOW = 1792454400;
class Backend : public HazardStorageBackend
{
public:
    std::vector<uint8_t> slots[2]; bool exists[2] = {false, false}; bool fail = false; size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *out, size_t cap, size_t &size) override
    {
        size = slots[slot].size();
        if (!exists[slot]) return SlotRead::Missing;
        if (size > cap) return SlotRead::Invalid;
        if (size) std::memcpy(out, slots[slot].data(), size);
        return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    { ++writes; exists[slot] = true; slots[slot].assign(data, data + (fail ? 8 : size)); return !fail; }
};
struct Counter
{
    uint32_t value = 0;
    static bool reserve(uint32_t value, void *data)
    { Counter &c = *static_cast<Counter *>(data); if (value <= c.value) return false; c.value = value; return true; }
};
PhoneHazardReport report(uint32_t at = NOW, double longitude = 0)
{ return PhoneHazardReport{HazardType::RoadObstacle, 3, 0, longitude, at, at, 5}; }
HazardObservation observation(uint32_t sequence, uint32_t node = 7, double longitude = 0)
{
    HazardObservation o = {};
    o.id = o.hazardId = (static_cast<uint64_t>(node) << 32) | sequence;
    o.observerNodeId = node; o.kind = ObservationKind::Report; o.type = HazardType::RoadObstacle;
    o.severity = 3; o.observedAt = o.locationFixAt = NOW;
    o.latitude = 0; o.longitude = longitude; o.locationAccuracyMeters = 5;
    return o;
}
HazardRecord record(const HazardObservation &o)
{
    HazardRecord r = {};
    r.id = o.hazardId; r.originNodeId = r.updatedByNodeId = static_cast<uint32_t>(r.id >> 32);
    r.type = o.type; r.severity = o.severity; r.latitude = o.latitude; r.longitude = o.longitude;
    r.createdAt = r.lastObservedAt = o.observedAt; r.locationFixAt = o.locationFixAt;
    r.locationAccuracyMeters = o.locationAccuracyMeters; r.expiresAt = NOW + 3600;
    r.version = 1; r.confidence = 50; r.status = HazardStatus::Unverified;
    return r;
}
void add(HazardRepository &repo, const HazardObservation &o)
{ assert(repo.add(record(o))); assert(repo.addObservation(o)); }
void testRetryAndHistory()
{
    Backend backend; HazardStorage storage(backend); HazardRepository repo; Counter counter;
    assert(storage.load(repo) == StorageLoad::Empty);
    HazardService service(repo, 7, 0, Counter::reserve, &counter, &storage);
    HazardId id;
    assert(service.create(report(), NOW, id, 100) == HazardResult::Ok);
    const HazardId first = id; const size_t writes = backend.writes;
    assert(service.create(report(), 0, id, 100) == HazardResult::Unchanged && id == first);
    assert(backend.writes == writes && counter.value == 1 && repo.observationCount() == 1);
    PhoneHazardReport changed = report(); changed.severity = 5;
    assert(service.create(changed, NOW, id, 100) == HazardResult::Conflict);
    assert(service.revise(first, report(NOW + 1), NOW + 1, 101) == HazardResult::Ok);
    assert(service.revise(first, report(NOW + 1), 0, 101) == HazardResult::Unchanged);
    assert(service.resolve(first, NOW + 2, NOW + 2, 102) == HazardResult::Ok);
    assert(service.resolve(first, NOW + 2, 0, 102) == HazardResult::Unchanged);
    assert(service.resolve(first, NOW + 3, NOW + 3, 102) == HazardResult::Conflict);
    assert(repo.observationCount() == 3 && counter.value == 3);
    const HazardObservation *original = repo.findObservation(first);
    assert(original && original->observedAt == NOW && original->kind == ObservationKind::Report);
    const HazardObservation *clearance = repo.findClientObservation(7, 102);
    assert(clearance && clearance->observedAt == NOW + 2 && clearance->kind == ObservationKind::Clearance);
    HazardStorage reboot(backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Loaded && restored.observationCount() == 3);
    HazardService restarted(restored, 7, counter.value, Counter::reserve, &counter, &reboot);
    assert(restarted.create(report(), 0, id, 100) == HazardResult::Unchanged && id == first);
    assert(restored.findById(first)->status == HazardStatus::Resolved);
    assert(restarted.create(report(NOW + 3), NOW + 3, id, 103) == HazardResult::Ok);
    HazardGroup groups[16];
    assert(HazardGroups::build(restored, groups, 16) == 1);
    assert(groups[0].memberCount == 2 && groups[0].observationCount == 4 &&
        groups[0].independentObservers == 1 && groups[0].mixedTerminalStates);
    assert(restored.findById(first)->status == HazardStatus::Resolved && restored.findById(id)->confidence == 50);
    std::cout << "PASS: keyed retries are idempotent across restart; revisions/clearances retain immutable history.\n";
}
void testGroups()
{
    HazardObservation a = observation(1), b = observation(2, 8, .0002);
    assert(HazardGroups::matches(a, b));
    b.observedAt = NOW + 120; assert(HazardGroups::matches(a, b));
    b.observedAt++; assert(!HazardGroups::matches(a, b));
    b = observation(2, 8, .0003); assert(!HazardGroups::matches(a, b));
    b = observation(2, 8); b.type = HazardType::Flooding; assert(!HazardGroups::matches(a, b));
    b = observation(2, 8); b.locationAccuracyMeters = 25; assert(HazardGroups::matches(a, b));
    b.locationAccuracyMeters = 26; assert(!HazardGroups::matches(a, b));
    HazardRepository left, right;
    a = observation(1, 7, 0); b = observation(1, 8, .00018);
    const HazardObservation c = observation(1, 9, .00036);
    add(left, a); add(left, b); add(left, c);
    add(right, c); add(right, b); add(right, a);
    HazardGroup ga[16], gb[16];
    assert(HazardGroups::build(left, ga, 16) == 2 && HazardGroups::build(right, gb, 16) == 2);
    for (size_t i = 0; i < 2; ++i)
    {
        assert(ga[i].canonicalId == gb[i].canonicalId && ga[i].memberCount == gb[i].memberCount);
        assert(ga[i].independentObservers == gb[i].independentObservers);
    }
    assert(ga[0].memberCount == 2 && ga[0].independentObservers == 2 && ga[1].memberCount == 1);
    assert(left.count() == 3 && left.observationCount() == 3); // No destructive coalescing/chained bridge.
    HazardGroup one[2] = {}; one[1].canonicalId = 123;
    assert(HazardGroups::build(left, one, 1) == 2 && one[1].canonicalId == 123);
    assert(HazardGroups::build(left, nullptr, 0) == 2);
    std::cout << "PASS: type/distance/time/accuracy matching, deterministic non-destructive grouping and no transitive bridge.\n";
}
void testImmutableImportsAndCapacity()
{
    HazardRepository repo; Counter counter;
    HazardService service(repo, 7, 0, Counter::reserve, &counter);
    HazardId id;
    assert(service.create(report(), NOW, id, 1) == HazardResult::Ok);
    const HazardId originalId = id;
    for (uint32_t i = 1; i < 32; ++i)
        assert(service.revise(originalId, report(NOW + i), NOW + i, i + 1) == HazardResult::Ok);
    assert(repo.observationCount() == 32 && repo.count() == 1 && counter.value == 32);
    assert(service.create(report(), NOW + 31, id, 1) == HazardResult::Unchanged);
    assert(service.create(report(NOW + 31), NOW + 31, id, 999) == HazardResult::EvidenceFull);
    assert(service.resolve(originalId, NOW + 31, NOW + 31, 1000) == HazardResult::EvidenceFull);
    assert(counter.value == 32 && repo.findById(originalId)->status != HazardStatus::Resolved);
    const HazardObservation original = *repo.findObservation(originalId);
    assert(service.importObservation(original) == HazardResult::Unchanged);
    HazardObservation altered = original; altered.severity++;
    assert(service.importObservation(altered) == HazardResult::Conflict);
    assert(service.importObservation(observation(1, 8)) == HazardResult::EvidenceFull);
    HazardRepository other; HazardService remote(other, 9, 0, Counter::reserve, &counter);
    assert(remote.importObservation(original) == HazardResult::Ok);
    assert(remote.importObservation(original) == HazardResult::Unchanged);
    assert(other.observationCount() == 1 && other.count() == 0); // Safe orphan until record arrives.
    assert(remote.importRemote(*repo.findById(originalId)) == HazardResult::Ok);
    assert(other.findObservation(originalId)->observedAt == NOW);
    altered = original; altered.id = altered.hazardId = (static_cast<uint64_t>(7) << 32) | 100;
    assert(remote.importObservation(altered) == HazardResult::Conflict); // Same observer/client key, new ID.
    std::cout << "PASS: immutable observation imports, relay deduplication, client-key collisions and evidence capacity limits.\n";
}
void testCodecMigrationAndFiles(const char *directory)
{
    HazardRepository source;
    for (uint32_t i = 1; i <= 16; ++i) add(source, observation(i));
    for (uint32_t i = 17; i <= 32; ++i)
    {
        HazardObservation o = observation(i); o.kind = ObservationKind::Revision;
        o.hazardId = observation(1).id; assert(source.addObservation(o));
    }
    std::vector<uint8_t> data(HazardSnapshot::MAX_SIZE); size_t size;
    assert(HazardSnapshot::encode(source, 1, data.data(), data.size(), size) && size == 2904);
    HazardRepository decoded; uint64_t generation;
    assert(HazardSnapshot::decode(data.data(), size, decoded, generation));
    assert(decoded.count() == 16 && decoded.observationCount() == 32);
    const std::string path = std::string(directory) + "/evidence-snapshot.bin";
    FILE *file = std::fopen(path.c_str(), "wb"); assert(file);
    assert(std::fwrite(data.data(), 1, size, file) == size && std::fclose(file) == 0);
    std::vector<uint8_t> reloaded(size);
    file = std::fopen(path.c_str(), "rb"); assert(file);
    assert(std::fread(reloaded.data(), 1, size, file) == size && std::fclose(file) == 0);
    assert(HazardSnapshot::decode(reloaded.data(), size, decoded, generation));
    assert(sameObservation(*source.findObservation(observation(32).id), *decoded.findObservation(observation(32).id)));
    reloaded.back() ^= 1;
    assert(!HazardSnapshot::decode(reloaded.data(), size, decoded, generation));
    HazardRepository legacy; assert(legacy.add(record(observation(1))));
    assert(HazardSnapshot::encode(legacy, 5, data.data(), data.size(), size));
    data[4] = 1; // A real schema-1 layout has zero observations and the same 60-byte records.
    const uint32_t crc = HazardSnapshot::checksum(data.data(), size);
    for (size_t i = 0; i < 4; ++i) data[20 + i] = static_cast<uint8_t>(crc >> (8 * i));
    assert(HazardSnapshot::decode(data.data(), size, decoded, generation));
    assert(decoded.count() == 1 && decoded.observationCount() == 0 && generation == 5);
    HazardGroup groups[16]; assert(HazardGroups::build(decoded, groups, 16) == 1);
    assert(groups[0].independentObservers == 0); // Do not fabricate independent legacy evidence.
    std::cout << "PASS: full evidence snapshot/host file round-trip, corruption rejection and schema-1 migration without invented history.\n";
}
void testFailureAndCommands()
{
    Backend backend; HazardStorage storage(backend); HazardRepository repo; Counter counter;
    assert(storage.load(repo) == StorageLoad::Empty && storage.save(repo));
    HazardService service(repo, 7, 0, Counter::reserve, &counter, &storage);
    backend.fail = true; HazardId id;
    assert(service.create(report(), NOW, id, 100) == HazardResult::RecordPersistenceFailed);
    assert(repo.count() == 0 && repo.observationCount() == 0 && counter.value == 1);
    backend.fail = false; assert(storage.load(repo) == StorageLoad::Recovered);
    assert(service.create(report(), NOW, id, 100) == HazardResult::Ok && static_cast<uint32_t>(id) == 2);
    assert(repo.count() == 1 && repo.observationCount() == 1);
    const char *commands[] = {"REPORT_ID 100 1 3 0 0 1792454400 1792454400 5",
        "REVISE_ID 101 30064771074 1 3 0 0 1792454401 1792454401 5",
        "RESOLVE_ID 102 30064771074 1792454402", "EVIDENCE 30064771074", "GROUPS"};
    for (const char *text : commands)
    {
        char line[160]; strcpy(line, text); HazardCommand command;
        assert(parseHazardCommand(line, command));
    }
    char invalid[] = "REPORT_ID 0 1 3 0 0 1792454400 1792454400 5"; HazardCommand command;
    assert(!parseHazardCommand(invalid, command));
    std::cout << "PASS: records/evidence commit atomically on save failure; keyed report and group console commands parse.\n";
}
}
void testHazardEvidence(const char *directory)
{
    testRetryAndHistory(); testGroups(); testImmutableImportsAndCapacity();
    testCodecMigrationAndFiles(directory); testFailureAndCommands();
}
