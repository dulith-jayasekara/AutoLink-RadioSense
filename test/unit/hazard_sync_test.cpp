#include "Hazard_Sync.h"
#include "Hazard_Groups.h"
#include "Hazard_Confidence.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
constexpr uint32_t NOW = 1792454400;
using Packet = std::vector<uint8_t>;
class Backend : public HazardStorageBackend
{
public:
    Packet slots[2]; bool exists[2] = {false, false}; bool fail = false; size_t writes = 0;
    SlotRead read(uint8_t slot, uint8_t *data, size_t cap, size_t &size) override
    {
        size = slots[slot].size();
        if (!exists[slot]) return SlotRead::Missing;
        if (size > cap) return SlotRead::Invalid;
        if (size) std::memcpy(data, slots[slot].data(), size);
        return SlotRead::Ok;
    }
    bool write(uint8_t slot, const uint8_t *data, size_t size) override
    {
        ++writes; exists[slot] = true; slots[slot].assign(data, data + (fail ? 8 : size)); return !fail;
    }
};
struct Node
{
    HazardRepository repo;
    Backend backend;
    HazardStorage storage;
    uint32_t sequence = 0;
    uint32_t id, session;
    bool busy = false;
    std::vector<Packet> outgoing;
    HazardService service;
    HazardSync sync;
    static bool reserve(uint32_t next, void *context)
    {
        Node &n = *static_cast<Node *>(context);
        if (next <= n.sequence) return false;
        n.sequence = next; return true;
    }
    static bool send(const uint8_t *data, size_t size, void *context)
    {
        Node &n = *static_cast<Node *>(context);
        if (n.busy) return false;
        assert(size <= HazardSyncCodec::MAX_FRAME && size <= 255);
        n.outgoing.emplace_back(data, data + size); return true;
    }
    explicit Node(uint32_t node) : storage(backend), id(node), session(node * 100 + 1),
        service(repo, node, 0, reserve, this, &storage), sync(repo, service, node, session, send, this)
    {
        assert(storage.load(repo) == StorageLoad::Empty);
    }
    HazardId seed()
    {
        PhoneHazardReport report{HazardType::RoadObstacle, 3, 6.9271, 79.8612, NOW, NOW - 2, 5};
        HazardId hazard;
        assert(service.create(report, NOW, hazard) == HazardResult::Ok);
        return hazard;
    }
};
Packet pack(const SyncFrame &frame)
{
    Packet bytes(HazardSyncCodec::MAX_FRAME); size_t size;
    assert(HazardSyncCodec::encode(frame, bytes.data(), bytes.size(), size)); bytes.resize(size); return bytes;
}
SyncFrame summary(const Node &source, const HazardRecord &record)
{
    SyncFrame f = {};
    f.type = SyncType::Summary; f.sender = source.id; f.session = source.session; f.sequence = 99;
    f.length = 17; f.payload[0] = 1;
    HazardSyncCodec::put(f.payload + 1, record.id, 8);
    HazardSyncCodec::put(f.payload + 9, record.version, 4);
    HazardSyncCodec::put(f.payload + 13, HazardSyncCodec::fingerprint(record), 4);
    return f;
}
struct Network
{
    struct Delivery { Node *to; Packet packet; uint32_t release; };
    std::vector<Delivery> delayed;
    uint32_t serial = 0;
    bool connected = true, faults = false, dropUpdates = false;
    void run(std::vector<Node *> nodes, uint32_t start, uint32_t duration)
    {
        for (uint32_t offset = 0; offset < duration; offset += 100)
        {
            const uint32_t now = start + offset;
            for (Node *node : nodes) node->sync.tick(now);
            for (Node *from : nodes)
            {
                for (const Packet &packet : from->outgoing)
                {
                    ++serial;
                    SyncFrame frame; assert(HazardSyncCodec::decode(packet.data(), packet.size(), frame));
                    if (!connected || (dropUpdates && frame.type == SyncType::Update) || (faults && serial % 4 == 0)) continue;
                    for (Node *to : nodes)
                        if (to != from && (!frame.target || frame.target == to->id))
                            delayed.push_back(Delivery{to, packet, now + (faults ? (serial % 5) * 700 : 0)});
                }
                from->outgoing.clear();
            }
            // Reverse delivery plus variable delays exercises out-of-order pages/responses.
            for (size_t i = delayed.size(); i > 0; --i)
            {
                Delivery &d = delayed[i - 1];
                if (static_cast<int32_t>(now - d.release) < 0) continue;
                d.to->sync.receive(d.packet.data(), d.packet.size(), now);
                if (faults) d.to->sync.receive(d.packet.data(), d.packet.size(), now);
                delayed.erase(delayed.begin() + static_cast<std::ptrdiff_t>(i - 1));
            }
        }
    }
};
void equal(const Node &a, const Node &b)
{
    assert(a.repo.count() == b.repo.count());
    assert(a.repo.observationCount() == b.repo.observationCount());
    for (size_t i = 0; i < a.repo.observationCount(); ++i)
    {
        HazardObservation observation; a.repo.observationAt(i, observation);
        const HazardObservation *other = b.repo.findObservation(observation.id);
        assert(other && sameObservation(observation, *other));
    }
    for (size_t i = 0; i < a.repo.count(); ++i)
    {
        HazardRecord record; a.repo.getAt(i, record);
        const HazardRecord *other = b.repo.findById(record.id);
        assert(other && sameHazardRecord(record, *other));
    }
}
void testCodec()
{
    Node a(7); const HazardId id = a.seed();
    const SyncFrame frame = summary(a, *a.repo.findById(id));
    const Packet bytes = pack(frame);
    assert(bytes.size() == 44); // One summary entry; full four-entry page is 92 bytes.
    SyncFrame decoded = {};
    assert(HazardSyncCodec::decode(bytes.data(), bytes.size(), decoded));
    assert(decoded.sender == 7 && decoded.length == 17);
    assert(bytes[4] == HazardSyncCodec::PROTOCOL_VERSION);
    Packet legacy = bytes; legacy[4] = 2;
    uint32_t legacyCrc = UINT32_MAX;
    for (size_t i = 0; i < legacy.size() - 4; ++i)
    {
        legacyCrc ^= legacy[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            legacyCrc = (legacyCrc >> 1) ^ ((legacyCrc & 1) ? 0xedb88320u : 0);
    }
    HazardSyncCodec::put(legacy.data() + legacy.size() - 4, ~legacyCrc, 4);
    assert(!HazardSyncCodec::decode(legacy.data(), legacy.size(), decoded));
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        Packet bad = bytes; bad[i] ^= 1;
        assert(!HazardSyncCodec::decode(bad.data(), bad.size(), decoded));
        assert(!HazardSyncCodec::decode(bytes.data(), i, decoded));
    }
    uint8_t tiny[4]; size_t size;
    assert(!HazardSyncCodec::encode(frame, tiny, sizeof(tiny), size));
    assert(!HazardSyncCodec::recognizes(nullptr, 100));
    SyncFrame invalid = frame; invalid.sender = 0;
    uint8_t out[119]; assert(!HazardSyncCodec::encode(invalid, out, sizeof(out), size));
    invalid = frame; invalid.payload[0] = 5;
    assert(!HazardSyncCodec::encode(invalid, out, sizeof(out), size));
    std::cout << "PASS: sync framing, CRC, truncation, length and identifier validation.\n";
}
void testTwoNodesAndRestart()
{
    Node a(7), b(8);
    for (int i = 0; i < 16; ++i) a.seed();
    Network network; network.run({&a, &b}, 0, 180000);
    equal(a, b); assert(b.repo.count() == 16 && b.sync.stats().imported == 16);
    const size_t writes = b.backend.writes;
    network.run({&a, &b}, 180000, 60000);
    assert(b.backend.writes == writes); // Repeated summaries do not rewrite unchanged records.
    HazardStorage restoredStorage(b.backend); HazardRepository restored;
    assert(restoredStorage.load(restored) == StorageLoad::Loaded && restored.count() == 16);
    HazardRecord record; a.repo.getAt(0, record);
    assert(sameHazardRecord(record, *restored.findById(record.id)));
    assert(restored.findById(record.id)->latitude == 6.9271);
    std::cout << "PASS: all 16 records synchronize across summary pages and survive storage reload.\n";
}
void testFaultsAndReconnect()
{
    Node a(7), b(8), c(9);
    for (int i = 0; i < 4; ++i) { a.seed(); b.seed(); }
    Network network; network.faults = true;
    network.run({&a, &b, &c}, 0, 240000);
    equal(a, b); equal(b, c); assert(c.repo.count() == 8);
    network.connected = false;
    const HazardId newId = a.seed();
    network.run({&a, &b, &c}, 240000, 40000);
    assert(!b.repo.findById(newId) && !c.repo.findById(newId));
    network.connected = true;
    network.run({&a, &b, &c}, 280000, 240000);
    equal(a, b); equal(b, c);
    assert(a.sync.stats().retries + b.sync.stats().retries + c.sync.stats().retries > 0);
    std::cout << "PASS: three-node convergence with 25% loss, duplication, delay/reordering and reconnection.\n";
}
void testRetryBusyAndWrap()
{
    Node a(7), b(8); a.seed();
    Network network; network.dropUpdates = true;
    network.run({&a, &b}, 0, 26000);
    assert(b.repo.count() == 0 && b.sync.stats().exhausted >= 1);
    assert(b.sync.stats().retries <= 2);
    network.dropUpdates = false;
    network.run({&a, &b}, 26000, 90000); equal(a, b);
    Node busyA(10), busyB(11); busyA.seed(); busyB.busy = true;
    Network blocked; blocked.run({&busyA, &busyB}, 0, 15000);
    assert(busyB.sync.stats().transportBusy > 0 && busyB.sync.stats().retries == 0);
    busyB.busy = false; blocked.run({&busyA, &busyB}, 15000, 90000); equal(busyA, busyB);
    Node wrapA(12), wrapB(13); wrapA.seed();
    Network wrapped; wrapped.run({&wrapA, &wrapB}, UINT32_MAX - 5000, 90000); equal(wrapA, wrapB);
    std::cout << "PASS: bounded retries, rediscovery after exhaustion, transport backpressure and millis rollover.\n";
}
void testCorrelationAndConflicts()
{
    Node a(7), b(8); const HazardId id = a.seed();
    const Packet advert = pack(summary(a, *a.repo.findById(id)));
    b.sync.receive(advert.data(), advert.size(), 0);
    b.sync.tick(0); b.sync.tick(2000);
    SyncFrame request = {}; bool found = false;
    for (const Packet &packet : b.outgoing)
    {
        SyncFrame frame; assert(HazardSyncCodec::decode(packet.data(), packet.size(), frame));
        if (frame.type == SyncType::Request) { request = frame; found = true; }
    }
    assert(found);
    SyncFrame update = {};
    update.type = SyncType::Update; update.sender = a.id; update.target = b.id;
    update.session = a.session; update.sequence = 555; update.length = 92;
    HazardSyncCodec::put(update.payload, b.session, 4);
    HazardSyncCodec::put(update.payload + 4, request.sequence, 4);
    assert(HazardSyncCodec::encodeRecord(*a.repo.findById(id), update.payload + 8));
    const Packet valid = pack(update); assert(valid.size() == 119);
    SyncFrame wrong = update; wrong.session++;
    Packet bad = pack(wrong); b.sync.receive(bad.data(), bad.size(), 2000); assert(b.repo.count() == 0);
    wrong = update; HazardSyncCodec::put(wrong.payload, b.session + 1, 4);
    bad = pack(wrong); b.sync.receive(bad.data(), bad.size(), 2000); assert(b.repo.count() == 0);
    wrong = update; wrong.target = 99;
    bad = pack(wrong); b.sync.receive(bad.data(), bad.size(), 2000); assert(b.repo.count() == 0);
    wrong = update; wrong.payload[40] ^= 1; // Valid outer CRC, corrupted embedded record.
    bad = pack(wrong); b.sync.receive(bad.data(), bad.size(), 2000); assert(b.repo.count() == 0);
    b.sync.receive(valid.data(), valid.size(), 2000);
    assert(b.repo.count() == 1 && b.sync.stats().imported == 1);
    const size_t writes = b.backend.writes;
    b.sync.receive(valid.data(), valid.size(), 2001);
    assert(b.backend.writes == writes && b.sync.stats().ignored >= 4);
    HazardRecord conflicting = *a.repo.findById(id); conflicting.severity = 5;
    assert(b.service.importRemote(conflicting) == HazardResult::Conflict);
    const Packet conflict = pack(summary(a, conflicting));
    b.sync.receive(conflict.data(), conflict.size(), 3000);
    assert(b.sync.stats().conflicts == 1 && b.repo.findById(id)->severity == 3);
    assert(a.service.resolve(id, NOW + 1, NOW + 1) == HazardResult::Ok);
    Network network; network.run({&a, &b}, 4000, 90000);
    assert(b.repo.findById(id)->status == HazardStatus::Resolved);
    HazardRecord reopen = *a.repo.findById(id); reopen.status = HazardStatus::Active; reopen.version += 100;
    assert(b.service.importRemote(reopen) == HazardResult::TerminalRecord);
    std::cout << "PASS: request/session correlation, duplicate idempotency, conflict reporting and terminal replication.\n";
}
void testCapacityAndFailure()
{
    Node a(7), b(8); const HazardId id = a.seed();
    assert(b.storage.save(b.repo)); // Ensure a prior empty snapshot exists for rollback.
    b.backend.fail = true;
    Network network; network.run({&a, &b}, 0, 25000);
    assert(b.repo.count() == 0 && b.sync.stats().rejected > 0 && !b.storage.writable());
    b.backend.fail = false;
    assert(b.storage.load(b.repo) == StorageLoad::Recovered);
    network.run({&a, &b}, 25000, 90000); equal(a, b);
    Node full(9);
    for (int i = 0; i < 16; ++i) full.seed();
    assert(full.service.importRemote(*a.repo.findById(id)) == HazardResult::Full);
    HazardRecord forged = *a.repo.findById(id);
    forged.id = (static_cast<HazardId>(9) << 32) | 99; forged.originNodeId = 9;
    assert(full.service.importRemote(forged) == HazardResult::PersistenceFailed);
    HazardRecord invalid = *a.repo.findById(id); invalid.latitude = 91;
    assert(b.service.importRemote(invalid) == HazardResult::InvalidReport);
    assert(b.service.importRemote(*a.repo.findById(id)) == HazardResult::Unchanged);
    std::cout << "PASS: failed remote saves do not publish records; capacity and local ID allocation stay protected.\n";
}

void testRevisionAndOriginDeparture()
{
    Node a(7), b(8), c(9);
    const HazardId id = a.seed();
    const HazardRecord old = *a.repo.findById(id);
    Network first; first.run({&a, &b}, 0, 60000); equal(a, b);
    PhoneHazardReport updated{HazardType::RoadObstacle, 4, 7.2, 80.1, NOW + 1, NOW, 4};
    assert(a.service.revise(id, updated, NOW + 1) == HazardResult::Ok);
    first.run({&a, &b}, 60000, 60000); equal(a, b);
    // The original reporter leaves; B retains and synchronizes its copy to C.
    Network second; second.run({&b, &c}, 120000, 90000); equal(b, c);
    assert(c.repo.findById(id)->version == 2 && c.repo.findById(id)->latitude == 7.2);
    assert(c.repo.findById(id)->originNodeId == a.id && c.repo.findById(id)->confidence == 50);
    assert(c.service.importRemote(old) == HazardResult::StaleUpdate);
    size_t changed;
    assert(c.service.processLifecycle(0, changed) == HazardResult::ClockUnavailable && changed == 0);
    std::cout << "PASS: newer observations synchronize, survive origin departure and preserve location/confidence without a clock.\n";
}

void testBoundedQueues()
{
    Node source(7), sink(8);
    const HazardId id = source.seed();
    const HazardRecord original = *source.repo.findById(id);
    for (uint32_t i = 0; i < 24; ++i)
    {
        HazardRecord other = original;
        other.id = (static_cast<HazardId>(100 + i) << 32) | 1;
        other.originNodeId = 100 + i;
        const Packet packet = pack(summary(source, other));
        sink.sync.receive(packet.data(), packet.size(), 0);
    }
    assert(sink.sync.stats().queueFull == 24 - HazardSync::REQUEST_CAPACITY);
    for (uint32_t i = 1; i <= 20; ++i)
    {
        SyncFrame f = {};
        f.type = SyncType::Request; f.sender = sink.id; f.target = source.id;
        f.session = sink.session; f.sequence = i; f.length = 16;
        HazardSyncCodec::put(f.payload, id, 8);
        HazardSyncCodec::put(f.payload + 8, original.version, 4);
        HazardSyncCodec::put(f.payload + 12, HazardSyncCodec::fingerprint(original), 4);
        const Packet packet = pack(f);
        source.sync.receive(packet.data(), packet.size(), 0);
    }
    assert(source.sync.stats().queueFull == 20 - HazardSync::RESPONSE_CAPACITY);
    assert(source.repo.count() == 1 && sink.repo.count() == 0);
    std::cout << "PASS: excess summary/request bursts respect fixed request/response queue limits.\n";
}

void testEvidenceGroupsAcrossReconnect()
{
    Node a(7), b(8), c(9);
    PhoneHazardReport first{HazardType::RoadObstacle, 3, 0, 0, NOW, NOW, 5};
    PhoneHazardReport second = first; second.longitude = .0001; second.observedAt = NOW + 10;
    HazardId aid, bid;
    assert(a.service.create(first, NOW, aid, 111) == HazardResult::Ok);
    assert(b.service.create(second, NOW + 10, bid, 111) == HazardResult::Ok); // Retry keys are observer-scoped.
    Network network; network.faults = true;
    network.run({&a, &b, &c}, 0, 240000); equal(a, b); equal(b, c);
    HazardGroup groups[16];
    assert(HazardGroups::build(c.repo, groups, 16) == 1);
    assert(groups[0].memberCount == 2 && groups[0].observationCount == 2 && groups[0].independentObservers == 2);
    assert(c.repo.findById(aid)->confidence == 50 && c.repo.findById(bid)->confidence == 50);
    network.connected = false;
    PhoneHazardReport revised = first; revised.observedAt = revised.locationFixAt = NOW + 20;
    assert(a.service.revise(aid, revised, NOW + 20, 112) == HazardResult::Ok);
    network.run({&a, &b, &c}, 240000, 40000);
    assert(b.repo.observationCount() == 2);
    network.connected = true;
    network.run({&a, &b, &c}, 280000, 240000); equal(a, b); equal(b, c);
    assert(c.service.resolve(aid, NOW + 30, NOW + 30, 113) == HazardResult::Ok);
    network.run({&a, &b, &c}, 520000, 240000); equal(a, b); equal(b, c);
    assert(HazardGroups::build(c.repo, groups, 16) == 1 && groups[0].observationCount == 4);
    assert(groups[0].independentObservers == 2 && groups[0].mixedTerminalStates);
    const size_t writes = a.backend.writes;
    HazardId retried;
    assert(a.service.create(first, 0, retried, 111) == HazardResult::Unchanged && retried == aid);
    assert(a.backend.writes == writes && a.repo.observationCount() == 4);
    HazardStorage restarted(c.backend); HazardRepository restored;
    assert(restarted.load(restored) == StorageLoad::Loaded && restored.observationCount() == 4);
    assert(HazardGroups::build(restored, groups, 16) == 1 && groups[0].independentObservers == 2);
    std::cout << "PASS: independent evidence/groups converge under loss/reconnect; relays and clearances do not inflate observer counts.\n";
}

void testConfidenceEvidenceSync()
{
    Node a(7), b(8), c(9); const HazardId id = a.seed();
    Network network; network.faults = true;
    network.run({&a, &b, &c}, 0, 180000); equal(a, b); equal(b, c);
    PhoneHazardReport input{HazardType::RoadObstacle, 3, 6.9271, 79.8612, NOW + 1, NOW + 1, 5};
    assert(b.service.assess(id, input, ObservationKind::Confirmation, NOW + 1, 100) == HazardResult::Ok);
    network.run({&a, &b, &c}, 180000, 180000); equal(a, b); equal(b, c);
    for (Node *node : {&a, &b, &c})
        assert(HazardConfidence::evaluate(node->repo, *node->repo.findById(id), NOW + 1).score == 70);
    network.connected = false;
    input.observedAt = input.locationFixAt = NOW + 2;
    assert(b.service.assess(id, input, ObservationKind::Contradiction, NOW + 2, 101) == HazardResult::Ok);
    network.run({&a, &b, &c}, 360000, 30000);
    assert(HazardConfidence::evaluate(a.repo, *a.repo.findById(id), NOW + 2).score == 70);
    network.connected = true;
    network.run({&a, &b, &c}, 390000, 180000); equal(a, b); equal(b, c);
    for (Node *node : {&a, &b, &c})
    {
        const size_t writes = node->backend.writes;
        const HazardRecord &record = *node->repo.findById(id);
        assert(HazardConfidence::evaluate(node->repo, record, NOW + 2).score == 25);
        assert(HazardConfidence::evaluate(node->repo, record, NOW + 900).score == 15);
        assert(node->backend.writes == writes && record.version == 1 && record.confidence == 50);
    }
    HazardStorage reboot(c.backend); HazardRepository restored;
    assert(reboot.load(restored) == StorageLoad::Loaded);
    assert(HazardConfidence::evaluate(restored, *restored.findById(id), NOW + 2).score == 25);
    std::cout << "PASS: confidence votes converge over three-node lossy/reordered LoRa sync and reconnection; scores survive snapshot reload without record churn.\n";
}
}
void testHazardSync()
{
    testCodec(); testTwoNodesAndRestart(); testFaultsAndReconnect();
    testRetryBusyAndWrap(); testCorrelationAndConflicts(); testCapacityAndFailure();
    testRevisionAndOriginDeparture(); testBoundedQueues();
    testEvidenceGroupsAcrossReconnect();
    testConfidenceEvidenceSync();
}
