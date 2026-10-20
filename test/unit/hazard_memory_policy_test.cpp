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
class Backend : public HazardStorageBackend
{
public:
    std::vector<uint8_t> slots[2]; size_t writes = 0; bool fail = false;
    SlotRead read(uint8_t slot, uint8_t *out, size_t cap, size_t &size) override
    {
        size = slots[slot].size(); if (!size) return SlotRead::Missing;
        if (size > cap) return SlotRead::Invalid;
        std::memcpy(out, slots[slot].data(), size); return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    { ++writes; slots[slot].assign(data, data + (fail ? 8 : size)); return !fail; }
};
struct Node
{
    HazardRepository repo; Backend backend; HazardStorage storage; uint32_t sequence = 0; HazardService service;
    static bool reserve(uint32_t next, void *data)
    { Node &node = *static_cast<Node *>(data); if (next <= node.sequence) return false; node.sequence = next; return true; }
    Node() : storage(backend), service(repo, 7, 0, reserve, this, &storage)
    { assert(storage.load(repo) == StorageLoad::Empty); }
};
PhoneHazardReport report(uint8_t severity, uint32_t at = NOW)
{ return PhoneHazardReport{HazardType::RoadDamage, severity, 0, 0, at, at, 5}; }
HazardRecord cached(uint32_t i)
{
    HazardRecord r = {};
    r.id = (static_cast<uint64_t>(8) << 32) | i; r.originNodeId = r.updatedByNodeId = 8;
    r.type = HazardType::RoadDamage; r.severity = 1; r.latitude = i; r.longitude = 0;
    r.createdAt = r.lastObservedAt = r.locationFixAt = NOW; r.locationAccuracyMeters = 5;
    r.expiresAt = NOW + 86400; r.version = 1; r.confidence = 50; r.status = HazardStatus::Unverified;
    return r;
}
HazardObservation observation(const HazardRecord &r, uint32_t sequence)
{
    HazardObservation o = {};
    o.id = (static_cast<uint64_t>(8) << 32) | sequence; o.observerNodeId = 8;
    o.hazardId = r.id; o.clientReportId = sequence; o.kind = ObservationKind::Revision;
    o.type = r.type; o.severity = r.severity; o.latitude = r.latitude; o.longitude = r.longitude;
    o.observedAt = o.locationFixAt = NOW; o.locationAccuracyMeters = 5; return o;
}
void fill(Node &n)
{
    for (uint32_t i = 1; i <= 16; ++i)
    {
        const HazardRecord r = cached(i);
        assert(n.service.importRemote(r) == HazardResult::Ok);
        assert(n.service.importObservation(observation(r, 100 + i)) == HazardResult::Ok);
        assert(n.service.importObservation(observation(r, 200 + i)) == HazardResult::Ok);
    }
}
void testPriorityAdmission()
{
    Node n; fill(n); assert(n.repo.observationCount() == 32);
    const size_t before = n.backend.writes; HazardId id;
    assert(n.service.create(report(1), NOW, id, 1) == HazardResult::Full && !id);
    assert(n.backend.writes == before && n.sequence == 0);
    assert(n.service.create(report(5), NOW, id, 2) == HazardResult::Ok);
    assert(n.repo.count() == 16 && n.repo.observationCount() == 31);
    assert(!n.repo.findById(cached(16).id) && n.service.evictedCount() == 1);
    assert(n.backend.writes == before + 1); // Eviction, evidence cleanup and insertion share a snapshot.
    assert(n.service.create(report(5), 0, id, 2) == HazardResult::Unchanged);
    HazardRecord r = cached(1);
    HazardRepository confidenceRepo; assert(confidenceRepo.add(r));
    HazardObservation first = observation(r, 900); assert(confidenceRepo.addObservation(first));
    const unsigned oneObserver = HazardMemory::priority(confidenceRepo, r, NOW);
    HazardObservation second = first; second.observerNodeId = 9;
    second.id = (static_cast<uint64_t>(9) << 32) | 1; assert(confidenceRepo.addObservation(second));
    assert(HazardMemory::priority(confidenceRepo, r, NOW) == oneObserver + 40);
    HazardRecord severe = r; severe.severity = 5;
    assert(HazardMemory::priority(n.repo, severe, NOW) > HazardMemory::priority(n.repo, r, NOW));
    assert(HazardMemory::priority(n.repo, r, NOW) > HazardMemory::priority(n.repo, r, NOW + 20000));
    assert(HazardMemory::priority(n.repo, r, NOW, true, 1, 0) >
           HazardMemory::priority(n.repo, r, NOW, true, 20, 0));
    assert(!n.service.setPosition(91, 0) && n.service.setPosition(1, 0));
    assert(n.service.positionKnown());
    HazardStorage reboot(n.backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Loaded && restored.sameContents(n.repo));
    std::cout << "PASS: severity/confidence/freshness/location priorities, deterministic pressure eviction, linked evidence reclamation and single verified commit.\n";
}
void testRetryPinsAndFailure()
{
    Node n; HazardId id;
    for (uint64_t i = 1; i <= 16; ++i) assert(n.service.create(report(1), NOW, id, i) == HazardResult::Ok);
    const uint32_t seq = n.sequence;
    assert(n.service.create(report(5, NOW + 300), NOW + 300, id, 100) == HazardResult::Full);
    assert(n.sequence == seq); // The entire accepted retry window is pinned.
    assert(n.service.create(report(5, NOW + 301), NOW + 301, id, 100) == HazardResult::Ok);
    assert(n.service.create(report(1), NOW + 301, id, 16) == HazardResult::InvalidReport);
    Node failed; fill(failed); const HazardRepository before = failed.repo;
    failed.backend.fail = true;
    assert(failed.service.create(report(5), NOW, id, 99) == HazardResult::RecordPersistenceFailed);
    assert(failed.repo.sameContents(before) && failed.service.evictedCount() == 0 && failed.sequence == 1);
    HazardStorage reboot(failed.backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Recovered && restored.sameContents(before));
    std::cout << "PASS: recent local retry keys are pinned, expired retries cannot create duplicates, and failed replacements preserve old records across reboot.\n";
}
void testRetainedTerminalMarkers()
{
    Node n; HazardId id; assert(n.service.create(report(2), NOW, id, 1) == HazardResult::Ok);
    const HazardRecord old = *n.repo.findById(id);
    HazardObservation oldEvidence; assert(n.repo.observationAt(0, oldEvidence));
    assert(n.service.resolve(id, NOW + 1, NOW + 1, 2) == HazardResult::Ok);
    size_t removed;
    assert(n.service.cleanupMemory(0, removed) == HazardResult::ClockUnavailable && removed == 0);
    assert(n.service.cleanupMemory(old.expiresAt + HazardMemory::RETENTION_SECONDS - 1, removed) == HazardResult::Ok && !removed);
    const size_t writes = n.backend.writes;
    assert(n.service.cleanupMemory(old.expiresAt + HazardMemory::RETENTION_SECONDS, removed) == HazardResult::Ok && removed == 1);
    assert(n.repo.count() == 0 && n.repo.observationCount() == 0 && n.repo.retirementCount() == 1);
    assert(n.repo.findRetirement(id)->status == HazardStatus::Resolved && n.backend.writes == writes + 1);
    assert(n.service.importRemote(old) == HazardResult::TerminalRecord);
    assert(n.service.importObservation(oldEvidence) == HazardResult::TerminalRecord);
    assert(n.service.revise(id, report(3, NOW + 10), NOW + 10, 3) == HazardResult::TerminalRecord);
    assert(n.service.cleanupMemory(UINT32_MAX, removed) == HazardResult::Ok && removed == 0 && n.backend.writes == writes + 1);
    HazardStorage reboot(n.backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Loaded && restored.sameContents(n.repo));
    HazardService restart(restored, 7, n.sequence, Node::reserve, &n, &reboot);
    assert(restart.importRemote(old) == HazardResult::TerminalRecord); // Unknown clock after reboot is safe.
    Node interrupted; HazardId interruptedId;
    assert(interrupted.service.create(report(2), NOW, interruptedId, 1) == HazardResult::Ok);
    assert(interrupted.service.resolve(interruptedId, NOW + 1, NOW + 1, 2) == HazardResult::Ok);
    const HazardRepository retained = interrupted.repo;
    interrupted.backend.fail = true;
    assert(interrupted.service.cleanupMemory(UINT32_MAX, removed) == HazardResult::RecordPersistenceFailed && !removed);
    assert(interrupted.repo.sameContents(retained) && !interrupted.repo.retirementCount());
    HazardStorage recovered(interrupted.backend); HazardRepository recoveredRepo;
    assert(recovered.load(recoveredRepo) == StorageLoad::Recovered && recoveredRepo.sameContents(retained));
    Node staleRemote; size_t changed;
    assert(staleRemote.service.processLifecycle(NOW + 172800, changed) == HazardResult::Ok);
    assert(staleRemote.service.importRemote(cached(1)) == HazardResult::Ok);
    assert(!staleRemote.repo.count() && staleRemote.repo.findRetirement(cached(1).id));
    assert(staleRemote.backend.writes == 1); // No intermediate full-record snapshot.
    Node blocked;
    for (uint32_t i = 1; i <= 64; ++i)
        assert(blocked.repo.retire(HazardRetirement{(static_cast<uint64_t>(9) << 32) | i, NOW, HazardStatus::Expired}));
    HazardRecord r = cached(1); r.status = HazardStatus::Resolved; assert(blocked.repo.add(r));
    assert(blocked.service.cleanupMemory(UINT32_MAX, removed) == HazardResult::Ok && !removed);
    assert(blocked.repo.findById(r.id)); // Never overwrite a terminal marker or silently lose protection.
    std::cout << "PASS: terminal history retention boundaries, compact persistent anti-replay markers, unknown-clock reboot protection, and safe marker-capacity refusal.\n";
}
void testMarkerCodecAndAtomicBatch()
{
    Node n; fill(n);
    for (uint32_t i = 1; i <= 64; ++i)
        assert(n.repo.retire(HazardRetirement{(static_cast<uint64_t>(9) << 32) | i, NOW, HazardStatus::Expired}));
    std::vector<uint8_t> bytes(HazardSnapshot::MAX_SIZE); size_t size;
    assert(HazardSnapshot::encode(n.repo, 1, bytes.data(), bytes.size(), size) && size == bytes.size());
    HazardRepository restored; uint64_t generation;
    assert(HazardSnapshot::decode(bytes.data(), size, restored, generation) && restored.sameContents(n.repo));
    for (size_t cut = size - 13; cut < size; ++cut)
        assert(!HazardSnapshot::decode(bytes.data(), cut, restored, generation));
    bytes[4] = 3;
    const uint32_t checksum = HazardSnapshot::checksum(bytes.data(), size);
    for (unsigned i = 0; i < 4; ++i) bytes[20 + i] = static_cast<uint8_t>(checksum >> (8 * i));
    assert(!HazardSnapshot::decode(bytes.data(), size, restored, generation));
    Node batch;
    const HazardRetirement markers[] = {{cached(1).id, NOW, HazardStatus::Expired}, {cached(2).id, NOW, HazardStatus::Active}};
    assert(batch.service.importRetirements(markers, 2) == HazardResult::InvalidReport && !batch.repo.retirementCount());
    const HazardRetirement valid{cached(1).id, NOW, HazardStatus::Resolved};
    assert(batch.service.importRetirements(&valid, 1) == HazardResult::Ok);
    const size_t writes = batch.backend.writes;
    assert(batch.service.importRetirements(&valid, 1) == HazardResult::Unchanged && batch.backend.writes == writes);
    for (const char *text : {"CLEANUP", "RETIRED", "POSITION 6.9 79.8"})
    { char input[64]; std::strcpy(input, text); HazardCommand command; assert(parseHazardCommand(input, command)); }
    char invalid[] = "POSITION 91 0"; HazardCommand command; assert(!parseHazardCommand(invalid, command));
    std::cout << "PASS: maximum memory snapshot, legacy-schema rejection of retirement trailers, atomic marker batches, duplicate write suppression and memory commands.\n";
}
}
void testHazardMemoryPolicy()
{
    testPriorityAdmission(); testRetryPinsAndFailure(); testRetainedTerminalMarkers(); testMarkerCodecAndAtomicBatch();
}
