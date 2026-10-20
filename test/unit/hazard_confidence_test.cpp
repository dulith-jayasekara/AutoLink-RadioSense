#include "Hazard_Confidence.h"
#include "Hazard_Console.h"
#include "Hazard_Storage.h"
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
    std::vector<uint8_t> slots[2]; bool fail = false; size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *out, size_t capacity, size_t &size) override
    {
        size = slots[slot].size();
        if (!size) return SlotRead::Missing;
        if (size > capacity) return SlotRead::Invalid;
        std::memcpy(out, slots[slot].data(), size); return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    { ++writes; slots[slot].assign(data, data + (fail ? 8 : size)); return !fail; }
};
struct Node
{
    HazardRepository repo; Backend backend; HazardStorage storage;
    uint32_t sequence = 0; HazardService service;
    static bool reserve(uint32_t next, void *data)
    {
        Node &node = *static_cast<Node *>(data);
        if (next <= node.sequence) return false;
        node.sequence = next; return true;
    }
    explicit Node(uint32_t id) : storage(backend), service(repo, id, 0, reserve, this, &storage)
    { assert(storage.load(repo) == StorageLoad::Empty); }
};
PhoneHazardReport report(uint32_t at = NOW, double longitude = 0)
{ return PhoneHazardReport{HazardType::RoadObstacle, 3, 0, longitude, at, at, 5}; }
HazardId create(Node &node, uint64_t key = 1, uint32_t at = NOW, double longitude = 0)
{
    HazardId id;
    assert(node.service.create(report(at, longitude), at, id, key) == HazardResult::Ok);
    return id;
}
void copy(const Node &from, Node &to)
{
    for (size_t i = 0; i < from.repo.count(); ++i)
    {
        HazardRecord record; from.repo.getAt(i, record);
        const HazardResult result = to.service.importRemote(record);
        assert(result == HazardResult::Ok || result == HazardResult::Unchanged);
    }
    for (size_t i = 0; i < from.repo.observationCount(); ++i)
    {
        HazardObservation o; from.repo.observationAt(i, o);
        const HazardResult result = to.service.importObservation(o);
        assert(result == HazardResult::Ok || result == HazardResult::Unchanged);
    }
}
HazardConfidenceResult score(const HazardRepository &repo, HazardId id, uint32_t at = NOW + 20)
{ return HazardConfidence::evaluate(repo, *repo.findById(id), at); }
void vote(Node &node, HazardId id, uint64_t key, uint32_t at, bool against = false)
{
    assert(node.service.assess(id, report(at), against ? ObservationKind::Contradiction :
        ObservationKind::Confirmation, at, key) == HazardResult::Ok);
}
void testIndependentVotes()
{
    Node a(7), b(8); const HazardId id = create(a); copy(a, b);
    assert(score(a.repo, id).score == 50);
    vote(b, id, 10, NOW + 1);
    assert(score(b.repo, id).score == 70 && score(b.repo, id).supportingObservers == 2);
    assert(score(b.repo, id).effectiveStatus == HazardStatus::Active);
    vote(b, id, 11, NOW + 2);
    assert(score(b.repo, id).score == 70);
    const size_t writes = b.backend.writes; const uint32_t sequence = b.sequence;
    assert(b.service.assess(id, report(NOW + 2), ObservationKind::Confirmation, 0, 11) == HazardResult::Unchanged);
    assert(b.backend.writes == writes && b.sequence == sequence);
    assert(b.service.assess(id, report(NOW + 2), ObservationKind::Contradiction, NOW + 2, 11) == HazardResult::Conflict);
    vote(b, id, 12, NOW + 3, true);
    assert(score(b.repo, id).score == 25 && score(b.repo, id).contradictingObservers == 1);
    vote(b, id, 13, NOW + 3); // Equal-time positive cannot erase a negative.
    assert(score(b.repo, id).score == 25);
    HazardRepository reversed;
    HazardRecord record; b.repo.getAt(0, record); assert(reversed.add(record));
    for (size_t i = b.repo.observationCount(); i > 0; --i)
    { HazardObservation o; b.repo.observationAt(i - 1, o); assert(reversed.addObservation(o)); }
    assert(score(reversed, id).score == 25);
    vote(b, id, 14, NOW + 4);
    assert(score(b.repo, id).score == 70);
    copy(b, a);
    const size_t saved = a.backend.writes;
    copy(b, a);
    assert(score(a.repo, id).score == 70 && a.backend.writes == saved);
    assert(a.repo.findById(id)->confidence == 50 && a.repo.findById(id)->version == 1);
    Node c(9); copy(a, c); vote(c, id, 15, NOW + 5);
    assert(score(c.repo, id).score == 90);
    Node d(10); copy(c, d); vote(d, id, 16, NOW + 6);
    assert(score(d.repo, id).score == 100);
    Node negativeC(11); copy(b, negativeC); vote(negativeC, id, 20, NOW + 7, true);
    vote(negativeC, id, 21, NOW + 6); // Late-arriving older positive cannot replace the negative.
    assert(score(negativeC.repo, id).score == 45);
    Node negativeD(12); copy(negativeC, negativeD); vote(negativeD, id, 22, NOW + 8, true);
    assert(score(negativeD.repo, id).score == 20);
    Node negativeE(13); copy(negativeD, negativeE); vote(negativeE, id, 23, NOW + 9, true);
    assert(score(negativeE.repo, id).score == 0);
    assert(score(negativeE.repo, id).effectiveStatus == HazardStatus::Unverified);
    assert(std::strcmp(HazardConfidence::level(70), "High") == 0);
    assert(std::strcmp(HazardConfidence::level(50), "Medium") == 0);
    assert(std::strcmp(HazardConfidence::level(25), "Low") == 0);
    std::cout << "PASS: distinct-observer scoring, same-node/relay deduplication, latest votes, conservative ties and bounded scores.\n";
}
void testDecayAndTerminals()
{
    Node a(7), b(8); const HazardId id = create(a); copy(a, b); vote(b, id, 10, NOW + 1);
    const size_t writes = b.backend.writes;
    assert(score(b.repo, id, NOW + 899).score == 70);
    assert(score(b.repo, id, NOW + 900).score == 60);
    auto half = score(b.repo, id, NOW + 1800);
    assert(half.score == 50 && half.decayPenalty == 20 && half.effectiveStatus == HazardStatus::Stale);
    assert(score(b.repo, id, NOW + 2700).score == 40);
    assert(score(b.repo, id, NOW + 3600).score == 0);
    assert(score(b.repo, id, NOW + 3600).effectiveStatus == HazardStatus::Expired);
    auto unknown = score(b.repo, id, 0);
    assert(!unknown.timeKnown && unknown.decayPenalty == 0 && unknown.score == 70);
    assert(unknown.effectiveStatus == HazardStatus::Unverified);
    assert(!score(b.repo, id, NOW - 1).timeKnown);
    assert(b.backend.writes == writes && b.repo.findById(id)->version == 1);
    assert(b.service.resolve(id, NOW + 2, NOW + 2, 20) == HazardResult::Ok);
    assert(score(b.repo, id, 0).score == 0 && score(b.repo, id, 0).effectiveStatus == HazardStatus::Resolved);
    const size_t count = b.repo.observationCount();
    assert(b.service.assess(id, report(NOW + 3), ObservationKind::Confirmation, NOW + 3, 21) == HazardResult::TerminalRecord);
    assert(b.repo.observationCount() == count);
    std::cout << "PASS: quarter-TTL decay, unknown clocks, terminal zero scores and no flash/version writes from confidence queries.\n";
}
void testGroupsAndValidation()
{
    Node a(7), b(8); const HazardId id = create(a); create(b, 1, NOW + 1, .0001); copy(b, a);
    assert(score(a.repo, id).score == 70);
    const HazardId bid = b.repo.findClientObservation(8, 1)->hazardId;
    assert(b.service.resolve(bid, NOW + 5, NOW + 5, 2) == HazardResult::Ok);
    copy(b, a);
    assert(score(a.repo, id).score == 25); // A matching peer's clearance is negative presence evidence.
    // Use a fresh pair for geometric grouping validation.
    Node groupA(17), groupB(18); const HazardId groupId = create(groupA);
    create(groupB, 1, NOW + 1, .0001); copy(groupB, groupA);
    create(groupA, 2, NOW + 2, .00005); // Another raw record is still one vehicle.
    assert(score(groupA.repo, groupId).score == 70);
    Node c(19); create(c, 1, NOW + 1, .0003); copy(c, groupA); // No transitive geographic bridge.
    assert(score(groupA.repo, groupId).score == 70);
    const uint32_t sequence = a.sequence;
    auto input = report(NOW + 10); input.locationAccuracyMeters = 26;
    assert(a.service.assess(id, input, ObservationKind::Confirmation, NOW + 10, 30) == HazardResult::InvalidReport);
    input = report(NOW + 10, .01);
    assert(a.service.assess(id, input, ObservationKind::Confirmation, NOW + 10, 30) == HazardResult::InvalidReport);
    input = report(NOW + 10); input.type = HazardType::Flooding;
    assert(a.service.assess(id, input, ObservationKind::Confirmation, NOW + 10, 30) == HazardResult::InvalidReport);
    assert(a.service.assess(id, report(), ObservationKind::Report, NOW, 30) == HazardResult::InvalidReport);
    assert(a.service.assess(id, report(), ObservationKind::Confirmation, 0, 30) == HazardResult::ClockUnavailable);
    assert(a.service.assess(id, report(), ObservationKind::Confirmation, NOW, 0) == HazardResult::InvalidReport);
    assert(a.sequence == sequence);
    Node later(12); const HazardId laterId = create(later, 1, NOW + 10);
    assert(later.service.assess(laterId, report(NOW), ObservationKind::Confirmation, NOW + 10, 10) == HazardResult::StaleUpdate);
    Node future(13); copy(later, future); vote(future, laterId, 1, NOW + 20);
    assert(score(future.repo, laterId, NOW + 10).score == 50);
    assert(score(future.repo, laterId, NOW + 20).score == 70);
    std::cout << "PASS: geographic grouping, no transitive/same-vehicle inflation, GPS/type/time validation and future-vote exclusion.\n";
}
void testPersistenceAndFailure()
{
    Node a(7), b(8); const HazardId id = create(a); copy(a, b); vote(b, id, 10, NOW + 1);
    HazardStorage restarted(b.backend); HazardRepository restored;
    assert(restarted.load(restored) == StorageLoad::Loaded);
    assert(score(restored, id, NOW + 1).score == 70);
    HazardService service(restored, 8, b.sequence, Node::reserve, &b, &restarted);
    assert(service.assess(id, report(NOW + 1), ObservationKind::Confirmation, 0, 10) == HazardResult::Unchanged);
    assert(score(restored, id, 0).score == 70 && !score(restored, id, 0).timeKnown);
    const size_t count = b.repo.observationCount(); b.backend.fail = true;
    assert(b.service.assess(id, report(NOW + 2), ObservationKind::Contradiction, NOW + 2, 11) == HazardResult::RecordPersistenceFailed);
    assert(b.repo.observationCount() == count && score(b.repo, id).score == 70);
    assert(!b.repo.findClientObservation(8, 11) && !b.storage.writable());
    Node full(9); copy(a, full);
    for (uint32_t i = 1; i < HazardRepository::EVIDENCE_CAPACITY; ++i) vote(full, id, i, NOW + i);
    const uint32_t sequence = full.sequence;
    assert(full.service.assess(id, report(NOW + 40), ObservationKind::Confirmation, NOW + 40, 100) == HazardResult::EvidenceFull);
    assert(full.sequence == sequence && score(full.repo, id, NOW + 40).score == 70);
    std::cout << "PASS: vote snapshot/restart/retry recovery, atomic failed-save rollback and evidence capacity protection.\n";
}
void checksum(std::vector<uint8_t> &bytes)
{
    const uint32_t crc = HazardSnapshot::checksum(bytes.data(), bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[20 + i] = static_cast<uint8_t>(crc >> (8 * i));
}
void testMigrationAndConsole()
{
    Node a(7); const HazardId id = create(a);
    std::vector<uint8_t> bytes(HazardSnapshot::MAX_SIZE); size_t size;
    assert(HazardSnapshot::encode(a.repo, 1, bytes.data(), bytes.size(), size)); bytes.resize(size);
    bytes[4] = 2; checksum(bytes); HazardRepository restored; uint64_t generation;
    assert(HazardSnapshot::decode(bytes.data(), bytes.size(), restored, generation));
    assert(score(restored, id).score == 50 && restored.observationCount() == 1);
    vote(a, id, 2, NOW + 1);
    bytes.resize(HazardSnapshot::MAX_SIZE);
    assert(HazardSnapshot::encode(a.repo, 2, bytes.data(), bytes.size(), size)); bytes.resize(size);
    assert(bytes[4] == HazardSnapshot::SCHEMA_VERSION && HazardSnapshot::decode(bytes.data(), bytes.size(), restored, generation));
    bytes[4] = 3; checksum(bytes);
    assert(HazardSnapshot::decode(bytes.data(), bytes.size(), restored, generation));
    bytes[4] = 2; checksum(bytes);
    assert(!HazardSnapshot::decode(bytes.data(), bytes.size(), restored, generation));
    char confirm[] = "CONFIRM_ID 20 30064771073 1 3 0 0 1792454400 1792454400 5";
    char contradict[] = "CONTRADICT_ID 21 30064771073 1 3 0 0 1792454400 1792454400 5";
    char query[] = "CONFIDENCE 30064771073";
    char zeroKey[] = "CONFIRM_ID 0 30064771073 1 3 0 0 1792454400 1792454400 5";
    HazardCommand command;
    assert(parseHazardCommand(confirm, command) && command.action == ConsoleAction::Confirm && command.clientReportId == 20);
    assert(parseHazardCommand(contradict, command) && command.action == ConsoleAction::Contradict);
    assert(parseHazardCommand(query, command) && command.action == ConsoleAction::Confidence);
    assert(!parseHazardCommand(zeroKey, command));
    HazardObservation assessment = *a.repo.findClientObservation(7, 2);
    assessment.clientReportId = 0;
    assert(!HazardEvidenceCodec::valid(assessment));
    assessment = *a.repo.findClientObservation(7, 2); assessment.locationAccuracyMeters = 26;
    assert(!HazardEvidenceCodec::valid(assessment));
    std::cout << "PASS: schema-2 migration, schema-3 vote compatibility and confirmation/contradiction/query console commands.\n";
}
}
void testHazardConfidence()
{
    testIndependentVotes(); testDecayAndTerminals(); testGroupsAndValidation();
    testPersistenceAndFailure(); testMigrationAndConsole();
}
