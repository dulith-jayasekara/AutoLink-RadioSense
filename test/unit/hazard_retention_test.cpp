#include "Hazard_Retention.h"
#include "Hazard_Memory.h"
#include "Hazard_Service.h"
#include "Hazard_Storage.h"
#include "Hazard_Console.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
namespace
{
constexpr uint32_t NOW = 1792454400;
HazardRecord record(uint32_t sequence = 1)
{
    HazardRecord r = {};
    r.id = (static_cast<uint64_t>(8) << 32) | sequence; r.originNodeId = r.updatedByNodeId = 8;
    r.type = HazardType::RoadObstacle; r.severity = 3;
    r.createdAt = r.lastObservedAt = r.locationFixAt = NOW;
    r.expiresAt = NOW + 3600; r.locationAccuracyMeters = 5; r.version = 2;
    r.status = HazardStatus::Resolved; r.confidence = 50; return r;
}
class Backend : public HazardStorageBackend
{
public:
    std::vector<uint8_t> slots[2]; bool fail = false; size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *out, size_t cap, size_t &size) override
    {
        size = slots[slot].size(); if (!size) return SlotRead::Missing;
        if (size > cap) return SlotRead::Invalid;
        std::memcpy(out, slots[slot].data(), size); return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    { ++writes; slots[slot].assign(data, data + (fail ? 8 : size)); return !fail; }
};
void testViewsAndCapacity()
{
    HazardRepository repo; HazardRecord r = record(); assert(repo.add(r));
    const uint32_t deadline = r.expiresAt + HazardRetention::HISTORY_SECONDS;
    auto view = HazardRetention::evaluate(repo, r, 0);
    assert(view.phase == RetentionPhase::UnknownTime && !view.timeKnown);
    view = HazardRetention::evaluate(repo, r, deadline - 1);
    assert(view.phase == RetentionPhase::HistoryRetained && view.remainingSeconds == 1 && view.deadline == deadline);
    view = HazardRetention::evaluate(repo, r, deadline);
    assert(view.phase == RetentionPhase::Eligible && !view.remainingSeconds);
    r.status = HazardStatus::Unverified;
    assert(HazardRetention::evaluate(repo, r, NOW).phase == RetentionPhase::Ongoing);
    assert(HazardRetention::evaluate(repo, r, deadline).terminalStatus == HazardStatus::Expired);
    r = record(); r.expiresAt = UINT32_MAX - 10;
    view = HazardRetention::evaluate(repo, r, UINT32_MAX);
    assert(view.deadline == static_cast<uint64_t>(UINT32_MAX) + 86390 && view.phase == RetentionPhase::HistoryRetained);
    for (uint32_t i = 1; i <= 63; ++i)
        assert(repo.retire(HazardRetirement{(static_cast<uint64_t>(9) << 32) | i, NOW, HazardStatus::Expired}));
    assert(repo.add(record(2)));
    auto summary = HazardRetention::summarize(repo, deadline);
    assert(summary.fullTerminalCount == 2 && summary.eligibleCount == 2 && summary.readyCount == 1 && summary.blockedCount == 1);
    const HazardRepository before = repo;
    assert(HazardRetention::summarize(repo, 0).eligibleCount == 0 && repo.sameContents(before));
    assert(HazardMemory::cleanup(repo, deadline) == 1 && repo.count() == 1 && repo.retirementCount() == 64);
    summary = HazardRetention::summarize(repo, deadline);
    assert(summary.readyCount == 0 && summary.blockedCount == 1);
    assert(HazardRetention::evaluate(repo, record(2), deadline).phase == RetentionPhase::CapacityBlocked);
    char text[] = "RETENTION"; HazardCommand command;
    assert(parseHazardCommand(text, command) && command.action == ConsoleAction::Retention);
    char invalid[] = "RETENTION 1"; assert(!parseHazardCommand(invalid, command));
    std::cout << "PASS: retention deadlines, unknown time, overflow boundaries, read-only views and partial/full marker capacity accounting.\n";
}
void testTerminalPromotionAndReplay()
{
    Backend backend; HazardStorage storage(backend); HazardRepository repo;
    assert(storage.load(repo) == StorageLoad::Empty);
    const HazardRecord resolved = record();
    assert(repo.retire(HazardRetirement{resolved.id, resolved.createdAt, HazardStatus::Expired}));
    for (uint32_t i = 1; i <= 63; ++i)
        assert(repo.retire(HazardRetirement{(static_cast<uint64_t>(9) << 32) | i, NOW, HazardStatus::Expired}));
    assert(storage.save(repo)); HazardService service(repo, 7, 0, nullptr, nullptr, &storage);
    const size_t writes = backend.writes;
    assert(service.importRemote(resolved) == HazardResult::Ok);
    assert(repo.findRetirement(resolved.id)->status == HazardStatus::Resolved && !repo.count() && !repo.observationCount());
    assert(backend.writes == writes + 1);
    assert(service.importRemote(resolved) == HazardResult::TerminalRecord && backend.writes == writes + 1);
    HazardRecord stale = resolved; stale.status = HazardStatus::Active; stale.version = 100;
    assert(service.importRemote(stale) == HazardResult::TerminalRecord && service.blockedRecordImports() == 2);
    HazardObservation o = {}; o.id = o.hazardId = resolved.id; o.observerNodeId = 8;
    o.type = resolved.type; o.severity = 3; o.kind = ObservationKind::Report;
    o.observedAt = o.locationFixAt = NOW; o.locationAccuracyMeters = 5;
    assert(service.importObservation(o) == HazardResult::TerminalRecord && service.blockedEvidenceImports() == 1);
    HazardRecord mismatch = resolved; mismatch.createdAt--;
    assert(service.importRemote(mismatch) == HazardResult::Conflict && service.blockedRecordImports() == 2);
    HazardStorage reboot(backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Loaded && restored.sameContents(repo));
    Backend failedBackend; HazardStorage failedStorage(failedBackend); HazardRepository failedRepo;
    assert(failedStorage.load(failedRepo) == StorageLoad::Empty);
    assert(failedRepo.retire(HazardRetirement{record(2).id, NOW, HazardStatus::Expired}) && failedStorage.save(failedRepo));
    HazardService failedService(failedRepo, 7, 0, nullptr, nullptr, &failedStorage);
    const HazardRepository before = failedRepo; failedBackend.fail = true;
    assert(failedService.importRemote(record(2)) == HazardResult::RecordPersistenceFailed && failedRepo.sameContents(before));
    HazardStorage recover(failedBackend); HazardRepository recovered;
    assert(recover.load(recovered) == StorageLoad::Recovered && recovered.sameContents(before));
    std::cout << "PASS: clearance upgrades compact expiry markers without reopening history; replay counters, identity checks, reboot and failed-promotion rollback.\n";
}
}
void testHazardRetention() { testViewsAndCapacity(); testTerminalPromotionAndReplay(); }
