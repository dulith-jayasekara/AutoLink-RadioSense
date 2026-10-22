#include "Hazard_Console.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
constexpr uint32_t NOW = 1792454400;
HazardId makeId(uint32_t sequence, uint32_t node = 7)
{
    return (static_cast<HazardId>(node) << 32) | sequence;
}
PhoneHazardReport makeReport()
{
    return PhoneHazardReport{HazardType::RoadObstacle, 3, 6.9271, 79.8612, NOW, NOW - 2, 5.0f};
}
HazardRecord makeHazard(uint32_t sequence)
{
    HazardRecord r = {};
    r.id = makeId(sequence);
    r.originNodeId = r.updatedByNodeId = 7;
    r.type = HazardType::RoadObstacle;
    r.severity = 3;
    r.latitude = 6.9271;
    r.longitude = 79.8612;
    r.createdAt = r.lastObservedAt = NOW;
    r.locationFixAt = NOW - 2;
    r.locationAccuracyMeters = 5;
    r.expiresAt = NOW + 3600;
    r.version = 1;
    r.confidence = 50;
    r.status = HazardStatus::Unverified;
    return r;
}
struct SequenceStore
{
    uint32_t value = 0;
    bool fail = false;
    static bool reserve(uint32_t next, void *context)
    {
        SequenceStore &store = *static_cast<SequenceStore *>(context);
        if (store.fail || next <= store.value) return false;
        store.value = next;
        return true;
    }
};

void testValidation()
{
    const HazardRecord valid = makeHazard(1);
    assert(isValidHazardRecord(valid));
    HazardRecord r;
    r = valid; r.id = 0; assert(!isValidHazardRecord(r));
    r = valid; r.originNodeId = 8; assert(!isValidHazardRecord(r));
    r = valid; r.updatedByNodeId = 0; assert(!isValidHazardRecord(r));
    r = valid; r.id = makeId(0); assert(!isValidHazardRecord(r));
    r = valid; r.latitude = 90.01; assert(!isValidHazardRecord(r));
    r = valid; r.longitude = -180.01; assert(!isValidHazardRecord(r));
    r = valid; r.latitude = std::numeric_limits<double>::quiet_NaN(); assert(!isValidHazardRecord(r));
    r = valid; r.confidence = 101; assert(!isValidHazardRecord(r));
    r = valid; r.type = static_cast<HazardType>(255); assert(!isValidHazardRecord(r));
    r = valid; r.status = static_cast<HazardStatus>(255); assert(!isValidHazardRecord(r));
    r = valid; r.status = HazardStatus::Stale; assert(isValidHazardRecord(r));
    r = valid; r.severity = 0; assert(!isValidHazardRecord(r));
    r = valid; r.severity = 6; assert(!isValidHazardRecord(r));
    r = valid; r.createdAt = 0; assert(!isValidHazardRecord(r));
    r = valid; r.lastObservedAt--; assert(!isValidHazardRecord(r));
    r = valid; r.locationFixAt = NOW + 1; assert(!isValidHazardRecord(r));
    r = valid; r.locationAccuracyMeters = INFINITY; assert(!isValidHazardRecord(r));
    r = valid; r.expiresAt = NOW; assert(!isValidHazardRecord(r));
    r = valid; r.version = 0; assert(!isValidHazardRecord(r));
}

void testRepository()
{
    HazardRepository repo;
    const HazardRecord first = makeHazard(1);
    assert(repo.add(first));
    assert(!repo.add(first));
    assert(repo.count() == 1);
    assert(!repo.findById(makeId(99)));
    assert(!repo.update(first)); // Equal-version replay.
    HazardRecord updated = first;
    updated.version++;
    updated.confidence = 85;
    updated.status = HazardStatus::Active;
    assert(repo.update(updated));
    assert(repo.findById(first.id)->confidence == 85);
    assert(!repo.update(first)); // Stale version.
    HazardRecord invalid = updated;
    invalid.version++;
    invalid.lastObservedAt--;
    assert(!repo.update(invalid));
    invalid = updated; invalid.version++; invalid.createdAt--;
    assert(!repo.update(invalid));
    invalid = updated; invalid.version++; invalid.longitude = 181;
    assert(!repo.update(invalid));
    updated.version++;
    updated.status = HazardStatus::Resolved;
    assert(repo.update(updated));
    updated.version++;
    updated.status = HazardStatus::Active;
    assert(!repo.update(updated)); // Terminal marker cannot be resurrected.
    HazardRecord indexed;
    assert(repo.getAt(0, indexed) && indexed.id == first.id);
    assert(!repo.getAt(1, indexed));
    assert(repo.remove(first.id));
    assert(!repo.remove(first.id));
    assert(repo.count() == 0);
    for (uint32_t i = 1; i <= HazardRepository::CAPACITY; ++i) assert(repo.add(makeHazard(i)));
    assert(!repo.add(makeHazard(100)));
    assert(repo.remove(makeId(8)));
    assert(repo.count() == HazardRepository::CAPACITY - 1);
    assert(repo.findById(makeId(9)) && repo.findById(makeId(16)));
}

void testQueries()
{
    HazardRepository repo;
    assert(repo.add(makeHazard(1)));
    HazardRecord second = makeHazard(2);
    second.type = HazardType::Flooding;
    second.longitude += 1;
    assert(repo.add(second));
    assert(repo.add(makeHazard(3)));
    HazardRecord output[2] = {};
    output[1].id = 123;
    assert(repo.findByType(HazardType::RoadObstacle, output, 1) == 2);
    assert(output[0].id == makeId(1) && output[1].id == 123);
    assert(repo.findByType(HazardType::Flooding, nullptr, 0) == 1);
    assert(repo.findByType(HazardType::Accident, output, 2) == 0);
    assert(repo.findNearby(6.9271, 79.8612, 10, output, 1) == 2);
    assert(repo.findNearby(91, 0, 100, output, 2) == 0);
    assert(repo.findNearby(0, 0, NAN, output, 2) == 0);
    assert(repo.findNearby(0, 0, -1, output, 2) == 0);
    assert(hazardDistanceMeters(0, 0, 0, 0) == 0);
    assert(std::abs(hazardDistanceMeters(0, 0, 0, 1) - 111194.927) < 1);
    assert(hazardDistanceMeters(0, 179.999, 0, -179.999) < 223);
    assert(std::isfinite(hazardDistanceMeters(90, 0, -90, 180)));
}

void testService()
{
    HazardRepository repo;
    SequenceStore store;
    HazardService service(repo, 7, 0, SequenceStore::reserve, &store);
    PhoneHazardReport report = makeReport();
    HazardId id = 123;
    assert(service.create(report, 0, id) == HazardResult::ClockUnavailable && id == 0);
    assert(service.create(report, NOW, id) == HazardResult::Ok);
    assert(id == makeId(1) && store.value == 1);
    const HazardRecord *r = repo.findById(id);
    assert(r && r->confidence == 50 && r->status == HazardStatus::Unverified);
    assert(r->expiresAt == NOW + 3600 && r->originNodeId == 7);
    assert(service.revise(id, report, NOW) == HazardResult::StaleUpdate);
    report.observedAt++;
    report.severity = 4;
    assert(service.revise(id, report, NOW + 1) == HazardResult::Ok);
    assert(repo.findById(id)->version == 2 && repo.findById(id)->severity == 4);
    assert(repo.findById(id)->createdAt == NOW && repo.findById(id)->confidence == 50);
    assert(service.revise(makeId(99), report, NOW + 1) == HazardResult::NotFound);
    HazardRecord terminal = *repo.findById(id);
    terminal.version++;
    terminal.status = HazardStatus::Expired;
    assert(repo.update(terminal));
    assert(service.revise(id, report, NOW + 1) == HazardResult::TerminalRecord);
    // Simulated reboot: durable sequence is retained, RAM repository is lost.
    HazardRepository rebootRepo;
    HazardService reboot(rebootRepo, 7, store.value, SequenceStore::reserve, &store);
    assert(reboot.create(makeReport(), NOW, id) == HazardResult::Ok && id == makeId(3));
    HazardRepository otherRepo;
    SequenceStore otherStore;
    HazardService other(otherRepo, 8, 0, SequenceStore::reserve, &otherStore);
    HazardId otherId;
    assert(other.create(makeReport(), NOW, otherId) == HazardResult::Ok && otherId != makeId(1));
    store.fail = true;
    const size_t count = repo.count();
    assert(service.create(makeReport(), NOW, id) == HazardResult::PersistenceFailed && id == 0);
    assert(repo.count() == count);
    store.fail = false;
    // A bad restored counter must not overwrite an already stored identity.
    HazardService badCounter(repo, 7, 0, SequenceStore::reserve, &store);
    assert(badCounter.create(makeReport(), NOW, id) == HazardResult::PersistenceFailed);
    HazardService exhausted(repo, 7, UINT32_MAX, SequenceStore::reserve, &store);
    assert(exhausted.create(makeReport(), NOW, id) == HazardResult::SequenceExhausted);
    HazardService noStorage(repo, 7, 99, nullptr, nullptr);
    assert(noStorage.create(makeReport(), NOW, id) == HazardResult::PersistenceFailed);
    for (uint32_t i = 2; i <= HazardRepository::CAPACITY; ++i) assert(repo.add(makeHazard(i)));
    assert(service.create(makeReport(), NOW, id) == HazardResult::Full);
}

void testInputRejection()
{
    HazardRepository repo;
    SequenceStore store;
    HazardService service(repo, 7, 0, SequenceStore::reserve, &store);
    HazardId id;
    PhoneHazardReport r;
    r = makeReport(); r.latitude = NAN; assert(service.create(r, NOW, id) == HazardResult::InvalidReport);
    r = makeReport(); r.locationAccuracyMeters = 101; assert(service.create(r, NOW, id) == HazardResult::InvalidReport);
    r = makeReport(); r.locationFixAt = NOW - 121; assert(service.create(r, NOW, id) == HazardResult::InvalidReport);
    r = makeReport(); r.observedAt++; assert(service.create(r, NOW, id) == HazardResult::InvalidReport);
    r = makeReport(); assert(service.create(r, NOW + 301, id) == HazardResult::InvalidReport);
    r = makeReport(); r.observedAt = r.locationFixAt = UINT32_MAX;
    assert(service.create(r, UINT32_MAX, id) == HazardResult::InvalidReport);
    assert(store.value == 0 && repo.count() == 0);
    r = makeReport(); r.locationFixAt = NOW - 120; r.locationAccuracyMeters = 100;
    assert(service.create(r, NOW + 300, id) == HazardResult::Ok);
}

bool parse(const char *text, HazardCommand &command)
{
    char line[256];
    assert(strlen(text) < sizeof(line));
    strcpy(line, text);
    return parseHazardCommand(line, command);
}
void testConsoleAndClock()
{
    HazardCommand command;
    assert(parse("REPORT 1 3 6.9271 79.8612 1792454400 1792454398 5", command));
    assert(command.action == ConsoleAction::Report && command.report.observedAt == NOW);
    assert(parse("REVISE 30064771073 1 3 6.9271 79.8612 1792454401 1792454400 5", command));
    assert(command.id == makeId(1));
    assert(parse("GET 18446744073709551615", command));
    assert(parse("NEAR 6.9 79.8 100", command));
    assert(parse("TYPE 2", command));
    assert(parse("TIME 1792454400", command));
    assert(parse("LIST", command));
    const char *invalid[] = {"", "TIME -1", "TIME +1", "TIME 4294967296", "TIME 0", "TIME 12 x",
        "GET 18446744073709551616", "GET 1junk", "LIST extra", "TYPE 256", "NEAR nan 0 3", "NEAR 0 0 -1",
        "REPORT 257 3 0 0 12 12 5", "REPORT 1 3 nan 0 12 12 5", "REPORT 1 3 0 0 12 12 5 extra",
        "REPORT 1 3 0 0 12 12 1e100", "REPORT 1 3 0 0 12 12", "a b c d e f g h i j k"};
    for (const char *text : invalid) assert(!parse(text, command));
    PhoneClock clock;
    assert(clock.now(1000) == 0);
    assert(clock.set(NOW, UINT32_MAX - 500));
    assert(clock.now(499) == NOW + 1); // millis() rollover.
    assert(clock.now(999) == NOW + 1);
    assert(clock.now(1499) == NOW + 2);
    assert(!clock.set(NOW, 1499));
    assert(clock.set(NOW + 10, 1499));
    assert(clock.now(2499) == NOW + 11);
    PhoneClock overflow;
    assert(overflow.set(UINT32_MAX, 0));
    assert(overflow.now(1000) == 0);
}

void testSimulatedPhoneSession()
{
    // Exercise the real parser, clock, service and repository together.
    // SequenceStore substitutes for hardware NVS; this does not emulate flash.
    HazardRepository repo;
    SequenceStore store;
    HazardService service(repo, 7, 0, SequenceStore::reserve, &store);
    PhoneClock clock;
    HazardCommand command;
    HazardId id = 0;
    const char *report = "REPORT 1 3 6.9271 79.8612 1792454400 1792454398 5";
    assert(parse(report, command));
    assert(service.create(command.report, clock.now(0), id) == HazardResult::ClockUnavailable);
    std::cout << "PASS: phone report rejected before UTC is available.\n";

    assert(parse("TIME 1792454400", command));
    assert(clock.set(command.utc, 0));
    assert(parse(report, command));
    assert(service.create(command.report, clock.now(0), id) == HazardResult::Ok);
    assert(id == makeId(1));
    std::cout << "PASS: simulated phone GPS report creates hazard " << id << ".\n";

    assert(parse("GET 30064771073", command));
    const HazardRecord *record = repo.findById(command.id);
    assert(record && record->latitude == 6.9271 && record->longitude == 79.8612);
    assert(record->confidence == 50 && record->status == HazardStatus::Unverified);
    assert(parse("TYPE 1", command));
    assert(repo.findByType(command.type, nullptr, 0) == 1);
    assert(parse("NEAR 6.9271 79.8612 100", command));
    assert(repo.findNearby(command.latitude, command.longitude, command.radiusMeters, nullptr, 0) == 1);
    std::cout << "PASS: ID, type and location queries retrieve the report.\n";

    assert(parse("REVISE 30064771073 1 4 6.9271 79.8612 1792454410 1792454408 5", command));
    assert(service.revise(command.id, command.report, clock.now(10000)) == HazardResult::Ok);
    assert(repo.findById(id)->version == 2 && repo.findById(id)->severity == 4);
    assert(service.revise(command.id, command.report, clock.now(10000)) == HazardResult::StaleUpdate);
    assert(repo.findById(id)->version == 2);
    std::cout << "PASS: newer revision accepted; replay rejected without changing the record.\n";

    assert(parse("REPORT 1 3 6.9271 79.8612 1792454410 1792454000 5", command));
    HazardId rejectedId;
    assert(service.create(command.report, clock.now(10000), rejectedId) == HazardResult::InvalidReport);
    assert(repo.count() == 1 && store.value == 2);
    std::cout << "PASS: outdated phone GPS fix rejected without consuming an ID.\n";

    HazardRepository afterRestart;
    PhoneClock afterRestartClock;
    HazardService restarted(afterRestart, 7, store.value, SequenceStore::reserve, &store);
    assert(parse(report, command));
    assert(restarted.create(command.report, afterRestartClock.now(0), id) == HazardResult::ClockUnavailable);
    assert(afterRestart.count() == 0);
    assert(afterRestartClock.set(NOW, 0));
    assert(restarted.create(command.report, afterRestartClock.now(0), id) == HazardResult::Ok);
    assert(id == makeId(3));
    std::cout << "PASS: simulated restart clears RAM/time; retained sequence produces a new ID.\n";
}
}
void testHazardStorage(const char *directory);
void testHazardLifecycle();
void testHazardSync();
void testHazardEvidence(const char *directory);
void testHazardConfidence();
void testHazardMemoryPolicy();
void testHazardRetention();
int main(int argc, char **argv)
{
    assert(argc == 2);
    testValidation(); testRepository(); testQueries(); testService();
    testInputRejection(); testConsoleAndClock();
    testSimulatedPhoneSession();
    testHazardStorage(argv[1]);
    testHazardLifecycle();
    testHazardSync();
    testHazardEvidence(argv[1]);
    testHazardConfidence();
    testHazardMemoryPolicy();
    testHazardRetention();
    std::cout << "Hazard Memory host tests passed (records, reports, IDs, queries, parser, clock, storage, lifecycle, synchronization).\n";
}
