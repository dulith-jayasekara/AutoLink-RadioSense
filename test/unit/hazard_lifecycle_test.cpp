#include "Hazard_Lifecycle.h"
#include "Hazard_Console.h"
#include "Hazard_Storage.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
bool reserveFixture(uint32_t, void *) { return true; }
constexpr uint32_t NOW = 1792454400;
HazardRecord makeRecord(uint32_t sequence = 1)
{
    HazardRecord r = {};
    r.id = (static_cast<HazardId>(7) << 32) | sequence;
    r.originNodeId = r.updatedByNodeId = 7;
    r.type = HazardType::RoadObstacle; r.severity = 3;
    r.latitude = 6.9271; r.longitude = 79.8612;
    r.createdAt = r.lastObservedAt = r.locationFixAt = NOW;
    r.locationAccuracyMeters = 5;
    r.expiresAt = NOW + 3600;
    r.version = 1; r.confidence = 50; r.status = HazardStatus::Unverified;
    return r;
}
PhoneHazardReport report(uint32_t time)
{
    return PhoneHazardReport{HazardType::RoadObstacle, 3, 6.9271, 79.8612, time, time, 5};
}
class Backend : public HazardStorageBackend
{
public:
    std::vector<uint8_t> files[2];
    bool exists[2] = {false, false};
    bool fail = false;
    size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *data, size_t capacity, size_t &size) override
    {
        size = files[slot].size();
        if (!exists[slot]) return SlotRead::Missing;
        if (size > capacity) return SlotRead::Invalid;
        if (size) std::memcpy(data, files[slot].data(), size);
        return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    {
        ++writes;
        exists[slot] = true;
        files[slot].assign(data, data + (fail ? 8 : size));
        return !fail;
    }
};

void testBoundaries()
{
    HazardRecord r = makeRecord();
    uint32_t age = 123;
    assert(!HazardLifecycle::ageSeconds(r, 0, age) && age == 0);
    assert(!HazardLifecycle::ageSeconds(r, NOW - 1, age));
    assert(HazardLifecycle::ageSeconds(r, NOW, age) && age == 0);
    assert(HazardLifecycle::evaluate(r, NOW + 1799) == HazardStatus::Unverified);
    assert(HazardLifecycle::evaluate(r, NOW + 1800) == HazardStatus::Stale);
    assert(HazardLifecycle::evaluate(r, NOW + 3599) == HazardStatus::Stale);
    assert(HazardLifecycle::evaluate(r, NOW + 3600) == HazardStatus::Expired);
    r.confidence = 69;
    assert(HazardLifecycle::evaluate(r, NOW) == HazardStatus::Unverified);
    r.confidence = 70;
    assert(HazardLifecycle::evaluate(r, NOW) == HazardStatus::Active);
    assert(HazardLifecycle::evaluate(r, 0) == r.status);
    assert(HazardLifecycle::evaluate(r, NOW - 1) == r.status);
    r.expiresAt = NOW + 3;
    assert(HazardLifecycle::evaluate(r, NOW + 1) == HazardStatus::Active);
    assert(HazardLifecycle::evaluate(r, NOW + 2) == HazardStatus::Stale);
    assert(HazardLifecycle::evaluate(r, NOW + 3) == HazardStatus::Expired);
    r.status = HazardStatus::Resolved;
    assert(HazardLifecycle::evaluate(r, UINT32_MAX) == HazardStatus::Resolved);
    r.status = HazardStatus::Expired;
    assert(HazardLifecycle::evaluate(r, NOW - 1) == HazardStatus::Expired);
    assert(!strcmp(HazardLifecycle::statusName(HazardStatus::Expired), "Expired"));
    std::cout << "PASS: lifecycle time/threshold boundaries, unknown age and terminal states.\n";
}

void testTransitionsAndClearance()
{
    HazardRepository repo;
    const HazardRecord first = makeRecord();
    assert(repo.add(first));
    HazardService service(repo, 7, 1, reserveFixture, nullptr);
    size_t changed = 99;
    assert(service.processLifecycle(0, changed) == HazardResult::ClockUnavailable && changed == 0);
    assert(service.processLifecycle(NOW - 1, changed) == HazardResult::Ok && changed == 0);
    assert(service.processLifecycle(NOW + 1800, changed) == HazardResult::Ok && changed == 1);
    assert(repo.findById(first.id)->status == HazardStatus::Stale);
    assert(repo.findById(first.id)->version == 2);
    assert(service.processLifecycle(NOW + 1, changed) == HazardResult::Ok && changed == 0);
    assert(repo.findById(first.id)->status == HazardStatus::Stale);
    assert(service.processLifecycle(NOW + 1801, changed) == HazardResult::Ok && changed == 0);
    assert(service.revise(first.id, report(NOW + 1801), NOW + 1801) == HazardResult::Ok);
    assert(repo.findById(first.id)->status == HazardStatus::Unverified);
    assert(repo.findById(first.id)->expiresAt == NOW + 5401);
    assert(repo.findById(first.id)->createdAt == NOW && repo.findById(first.id)->version == 3);
    assert(service.resolve(first.id, NOW + 1800, NOW + 1802) == HazardResult::StaleUpdate);
    assert(service.resolve(first.id, NOW + 1803, NOW + 1802) == HazardResult::InvalidReport);
    assert(service.resolve(first.id, NOW + 1802, 0) == HazardResult::ClockUnavailable);
    assert(service.resolve(first.id + 1, NOW + 1802, NOW + 1802) == HazardResult::NotFound);
    assert(service.resolve(first.id, NOW + 1802, NOW + 1802) == HazardResult::Ok);
    const HazardRecord marker = *repo.findById(first.id);
    assert(marker.status == HazardStatus::Resolved && marker.version == 4);
    assert(marker.lastObservedAt == NOW + 1801 && marker.confidence == 50);
    assert(service.resolve(first.id, NOW + 1802, NOW + 1802) == HazardResult::Ok);
    assert(repo.findById(first.id)->version == 4);
    assert(service.processLifecycle(UINT32_MAX, changed) == HazardResult::Ok && changed == 0);
    assert(service.revise(first.id, report(NOW + 1803), NOW + 1803) == HazardResult::TerminalRecord);
    HazardRecord replay = marker;
    replay.version += 100; replay.status = HazardStatus::Active;
    assert(!repo.update(replay));

    HazardRepository expiredRepo;
    assert(expiredRepo.add(first));
    HazardService expired(expiredRepo, 7, 1, reserveFixture, nullptr);
    // A new observation cannot extend an already elapsed TTL before the timer runs.
    assert(expired.revise(first.id, report(NOW + 3600), NOW + 3600) == HazardResult::TerminalRecord);
    assert(expired.resolve(first.id, NOW + 3600, NOW + 3600) == HazardResult::TerminalRecord);
    assert(expired.processLifecycle(NOW + 3600, changed) == HazardResult::Ok && changed == 1);
    assert(expiredRepo.findById(first.id)->status == HazardStatus::Expired);
    replay = *expiredRepo.findById(first.id); replay.version++; replay.status = HazardStatus::Unverified;
    assert(!expiredRepo.update(replay));
    char command[] = "RESOLVE 30064771073 1792454401";
    HazardCommand parsed;
    assert(parseHazardCommand(command, parsed) && parsed.action == ConsoleAction::Resolve);
    assert(parsed.id == first.id && parsed.utc == NOW + 1);
    const char *bad[] = {"RESOLVE 0 12", "RESOLVE 1 -1", "RESOLVE 1 0", "RESOLVE 1 4294967296", "RESOLVE 1 12 extra"};
    for (const char *text : bad)
    {
        char line[80]; strcpy(line, text);
        assert(!parseHazardCommand(line, parsed));
    }
    std::cout << "PASS: stale refresh, clearance validation, idempotent resolution and reopening rejection.\n";
}

void testConfidenceStateAndOverflow()
{
    HazardRepository repo;
    HazardRecord r = makeRecord(); r.confidence = 70;
    assert(repo.add(r));
    HazardService service(repo, 7, 1, reserveFixture, nullptr);
    size_t changed;
    assert(service.processLifecycle(NOW, changed) == HazardResult::Ok && changed == 1);
    assert(repo.findById(r.id)->status == HazardStatus::Active);
    r = *repo.findById(r.id); r.confidence = 69; r.version++;
    assert(repo.update(r)); // Fixture representing a later confidence engine update.
    assert(service.processLifecycle(NOW + 1, changed) == HazardResult::Ok && changed == 1);
    assert(repo.findById(r.id)->status == HazardStatus::Unverified);
    r = makeRecord(2); r.version = UINT32_MAX;
    assert(repo.add(r));
    const uint32_t before = repo.findById(makeRecord().id)->version;
    assert(service.processLifecycle(NOW + 3600, changed) == HazardResult::VersionExhausted && changed == 0);
    assert(repo.findById(makeRecord().id)->version == before); // Entire batch remains unchanged.
    assert(repo.findById(r.id)->status == HazardStatus::Unverified);
    assert(service.resolve(r.id, NOW + 1, NOW + 1) == HazardResult::VersionExhausted);
    assert(service.revise(r.id, report(NOW + 1), NOW + 1) == HazardResult::VersionExhausted);
    std::cout << "PASS: confidence-based state eligibility and atomic version-overflow rejection.\n";
}

void testPersistenceAndFailure()
{
    Backend backend;
    HazardStorage storage(backend);
    HazardRepository repo;
    assert(storage.load(repo) == StorageLoad::Empty);
    assert(repo.add(makeRecord()) && repo.add(makeRecord(2)) && storage.save(repo));
    HazardService service(repo, 7, 2, reserveFixture, nullptr, &storage);
    size_t changed;
    const size_t writes = backend.writes;
    assert(service.processLifecycle(0, changed) == HazardResult::ClockUnavailable && changed == 0);
    assert(service.processLifecycle(NOW + 1799, changed) == HazardResult::Ok && changed == 0);
    assert(backend.writes == writes);
    backend.fail = true;
    assert(service.processLifecycle(NOW + 1800, changed) == HazardResult::RecordPersistenceFailed && changed == 0);
    assert(repo.findById(makeRecord().id)->status == HazardStatus::Unverified);
    assert(repo.findById(makeRecord(2).id)->status == HazardStatus::Unverified);
    backend.fail = false;
    assert(storage.load(repo) == StorageLoad::Recovered);
    const size_t beforeBatch = backend.writes;
    assert(service.processLifecycle(NOW + 1800, changed) == HazardResult::Ok && changed == 2);
    assert(backend.writes == beforeBatch + 1); // One snapshot for both transitions.
    const size_t afterBatch = backend.writes;
    for (uint32_t t = NOW + 1801; t < NOW + 1810; ++t)
        assert(service.processLifecycle(t, changed) == HazardResult::Ok && changed == 0);
    assert(backend.writes == afterBatch);
    HazardStorage restoredStorage(backend);
    HazardRepository restored;
    assert(restoredStorage.load(restored) == StorageLoad::Loaded);
    HazardService restarted(restored, 7, 2, reserveFixture, nullptr, &restoredStorage);
    assert(restarted.processLifecycle(0, changed) == HazardResult::ClockUnavailable && changed == 0);
    assert(restored.findById(makeRecord().id)->status == HazardStatus::Stale);
    assert(restarted.processLifecycle(NOW + 100, changed) == HazardResult::Ok && changed == 0);
    assert(restored.findById(makeRecord().id)->status == HazardStatus::Stale);
    assert(restarted.processLifecycle(NOW + 3600, changed) == HazardResult::Ok && changed == 2);
    assert(restored.findById(makeRecord().id)->version == 3);
    HazardStorage finalStorage(backend);
    HazardRepository finalRepo;
    assert(finalStorage.load(finalRepo) == StorageLoad::Loaded);
    assert(finalRepo.count() == 2 && finalRepo.findById(makeRecord().id)->status == HazardStatus::Expired);
    assert(finalRepo.findById(makeRecord().id)->lastObservedAt == NOW);
    HazardService finalService(finalRepo, 7, 2, reserveFixture, nullptr, &finalStorage);
    assert(finalService.revise(makeRecord().id, report(NOW + 3601), NOW + 3601) == HazardResult::TerminalRecord);

    Backend clearBackend;
    HazardStorage clearStorage(clearBackend);
    HazardRepository clearRepo;
    assert(clearStorage.load(clearRepo) == StorageLoad::Empty);
    assert(clearRepo.add(makeRecord()) && clearStorage.save(clearRepo));
    HazardService clearer(clearRepo, 7, 1, reserveFixture, nullptr, &clearStorage);
    clearBackend.fail = true;
    assert(clearer.resolve(makeRecord().id, NOW + 1, NOW + 1) == HazardResult::RecordPersistenceFailed);
    assert(clearRepo.findById(makeRecord().id)->status == HazardStatus::Unverified);
    clearBackend.fail = false;
    assert(clearStorage.load(clearRepo) == StorageLoad::Recovered);
    assert(clearer.resolve(makeRecord().id, NOW + 1, NOW + 1) == HazardResult::Ok);
    const size_t clearWrites = clearBackend.writes;
    assert(clearer.resolve(makeRecord().id, NOW + 1, NOW + 1) == HazardResult::Ok);
    assert(clearBackend.writes == clearWrites);
    HazardStorage clearReboot(clearBackend);
    HazardRepository clearRestored;
    assert(clearReboot.load(clearRestored) == StorageLoad::Loaded);
    assert(clearRestored.findById(makeRecord().id)->status == HazardStatus::Resolved);
    std::cout << "PASS: lifecycle/clearance snapshots survive restart, unknown time preserves state, failed batches roll back.\n";
}
}
void testHazardLifecycle()
{
    testBoundaries(); testTransitionsAndClearance(); testConfidenceStateAndOverflow(); testPersistenceAndFailure();
}
