#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"
#include "Hazard Memory Network/Hazard_Console.h"
#include "Hazard Memory Network/Hazard_LittleFS.h"
#include "Hazard Memory Network/Hazard_Lifecycle.h"
#include "Hazard Memory Network/Hazard_Sync.h"
#include "Hazard Memory Network/Hazard_Groups.h"
#include "Hazard Memory Network/Hazard_Confidence.h"
#include "Hazard Memory Network/Hazard_Conflict.h"
#include "Hazard Memory Network/Hazard_Memory.h"
#include "Core/LoRa_Transport.h"
#include "Core/WiFi_Console.h"
#include <Preferences.h>
#include <new>

namespace
{
Print *commandOutput = &Serial;
HazardRepository repository;
HazardLittleFS storageBackend;
HazardStorage storage(storageBackend);
Preferences identity;
bool identityReady = false;
PhoneClock phoneClock;
alignas(HazardService) unsigned char serviceStorage[sizeof(HazardService)];
HazardService *service = nullptr;
#if HAZARD_ENABLE_LORA
alignas(HazardSync) unsigned char syncStorage[sizeof(HazardSync)];
HazardSync *hazardSync = nullptr;
bool sendHazardFrame(const uint8_t *data, size_t size, void *)
{
    return LoRaTransport::send(data, size);
}
bool receiveHazardFrame(const uint8_t *data, size_t size, void *)
{
    if (!HazardSyncCodec::recognizes(data, size)) return false;
    hazardSync->receive(data, size, millis());
    return true;
}
#endif
char line[192];
size_t lineLength = 0;
bool droppingLine = false;
uint32_t lastLifecycleTick = 0;

bool reserveSequence(uint32_t sequence, void *)
{
    return identityReady && identity.putUInt("sequence", sequence) == sizeof(uint32_t);
}

void help()
{
    commandOutput->println("TIME <unix_seconds>");
    commandOutput->println("REPORT <type:1-5> <severity:1-5> <lat> <lon> <observed_utc> <fix_utc> <accuracy_m>");
    commandOutput->println("REVISE <id> <same seven report fields>");
    commandOutput->println("RESOLVE <id> <clearance_utc> (local controlled-test clearance)");
    commandOutput->println("REPORT_ID <client_id> <report fields> | REVISE_ID <client_id> <id> <report fields>");
    commandOutput->println("RESOLVE_ID <client_id> <id> <clearance_utc> | GROUPS | EVIDENCE <id>");
    commandOutput->println("CONFIRM_ID / CONTRADICT_ID <client_id> <id> <report fields> | CONFIDENCE <id>");
    commandOutput->println("CLEANUP | RETIRED | POSITION <phone_lat> <phone_lon> (optional priority context)");
    commandOutput->println("GET <id> | LIST | TYPE <type> | NEAR <lat> <lon> <radius_m> | MEMORY | SYNC | HELP");
    commandOutput->println("Phone GPS is simulated by REPORT. Accepted changes are saved to LittleFS.");
}

void printRecord(const HazardRecord &r)
{
    const uint32_t now = phoneClock.now(millis());
    uint32_t age;
    const bool knownAge = HazardLifecycle::ageSeconds(r, now, age);
    char ageText[16] = "UNKNOWN";
    if (knownAge) snprintf(ageText, sizeof(ageText), "%lu", static_cast<unsigned long>(age));
    const bool terminal = r.status == HazardStatus::Resolved || r.status == HazardStatus::Expired;
    const HazardConfidenceResult confidence = HazardConfidence::evaluate(repository, r, now);
    const char *effective = knownAge || terminal ?
        HazardLifecycle::statusName(confidence.effectiveStatus) : "UnknownTime";
    commandOutput->printf("id=%llu origin=%lu updater=%lu type=%u severity=%u lat=%.6f lon=%.6f "
                  "created=%lu observed=%lu fix=%lu accuracy=%.1f expires=%lu version=%lu confidence=%u "
                  "status=%u(%s) effective=%s age_seconds=%s effective_confidence=%u "
                  "evidence_score=%u decay_penalty=%u supporting_observers=%u contradicting_observers=%u "
                  "confidence_time_known=%u confidence_level=%s priority=%u\n",
                  static_cast<unsigned long long>(r.id), static_cast<unsigned long>(r.originNodeId),
                  static_cast<unsigned long>(r.updatedByNodeId), static_cast<unsigned>(r.type),
                  static_cast<unsigned>(r.severity),
                  r.latitude, r.longitude, static_cast<unsigned long>(r.createdAt),
                  static_cast<unsigned long>(r.lastObservedAt), static_cast<unsigned long>(r.locationFixAt),
                  r.locationAccuracyMeters, static_cast<unsigned long>(r.expiresAt),
                  static_cast<unsigned long>(r.version), static_cast<unsigned>(r.confidence),
                  static_cast<unsigned>(r.status), HazardLifecycle::statusName(r.status), effective, ageText,
                  static_cast<unsigned>(confidence.score), static_cast<unsigned>(confidence.evidenceScore),
                  static_cast<unsigned>(confidence.decayPenalty), static_cast<unsigned>(confidence.supportingObservers),
                  static_cast<unsigned>(confidence.contradictingObservers), static_cast<unsigned>(confidence.timeKnown),
                  HazardConfidence::level(confidence.score), service->priority(r, now));
}

void runLifecycle(uint32_t now, bool verbose = false)
{
    static HazardResult previous = HazardResult::Ok;
    size_t changed = 0;
    const HazardResult result = service->processLifecycle(now, changed);
    if (verbose || changed || (result != HazardResult::Ok &&
        result != HazardResult::ClockUnavailable && result != previous))
        commandOutput->printf("lifecycle=%s changed=%u\n", hazardResultName(result), static_cast<unsigned>(changed));
    previous = result;
}

void execute(char *input)
{
    HazardCommand c;
    if (!parseHazardCommand(input, c)) { commandOutput->println("INVALID_COMMAND; enter HELP"); return; }
    const uint32_t now = phoneClock.now(millis());
    if (c.action != ConsoleAction::Time) runLifecycle(now);
    switch (c.action)
    {
    case ConsoleAction::Confidence:
    {
        const HazardRecord *record = repository.findById(c.id);
        if (record) printRecord(*record);
        else if (repository.findRetirement(c.id)) commandOutput->println("TERMINAL_RECORD_RETIRED; enter RETIRED");
        else commandOutput->println("NOT_FOUND");
        break;
    }
    case ConsoleAction::Confirm:
    case ConsoleAction::Contradict:
    {
        const ObservationKind kind = c.action == ConsoleAction::Confirm ?
            ObservationKind::Confirmation : ObservationKind::Contradiction;
        const HazardResult result = service->assess(c.id, c.report, kind, now, c.clientReportId);
        commandOutput->println(hazardResultName(result));
        if (result == HazardResult::Ok || result == HazardResult::Unchanged)
        {
            const HazardRecord *record = repository.findById(c.id);
            if (record) printRecord(*record);
        }
        break;
    }
    case ConsoleAction::Groups:
    {
        HazardGroup groups[HazardRepository::CAPACITY];
        const size_t count = HazardGroups::build(repository, groups, HazardRepository::CAPACITY);
        for (size_t i = 0; i < count; ++i)
        {
            const HazardGroup &g = groups[i];
            commandOutput->printf("group=%llu members=%u observations=%u observers=%u mixed_terminal=%u ids=",
                static_cast<unsigned long long>(g.canonicalId), static_cast<unsigned>(g.memberCount),
                static_cast<unsigned>(g.observationCount), static_cast<unsigned>(g.independentObservers),
                static_cast<unsigned>(g.mixedTerminalStates));
            for (size_t j = 0; j < g.memberCount; ++j)
                commandOutput->printf("%llu%s", static_cast<unsigned long long>(g.members[j]), j + 1 == g.memberCount ? "\n" : ",");
        }
        commandOutput->printf("groups=%u raw_records=%u\n", static_cast<unsigned>(count), static_cast<unsigned>(repository.count()));
        break;
    }
    case ConsoleAction::Evidence:
        for (size_t i = 0; i < repository.observationCount(); ++i)
        {
            HazardObservation o; repository.observationAt(i, o);
            if (o.hazardId != c.id) continue;
            commandOutput->printf("observation=%llu observer=%lu client_id=%llu kind=%u observed=%lu "
                          "lat=%.6f lon=%.6f severity=%u hazard_id=%llu type=%u fix=%lu accuracy=%.1f\n", static_cast<unsigned long long>(o.id),
                static_cast<unsigned long>(o.observerNodeId), static_cast<unsigned long long>(o.clientReportId),
                static_cast<unsigned>(o.kind), static_cast<unsigned long>(o.observedAt),
                o.latitude, o.longitude, static_cast<unsigned>(o.severity),
                static_cast<unsigned long long>(o.hazardId), static_cast<unsigned>(o.type),
                static_cast<unsigned long>(o.locationFixAt), o.locationAccuracyMeters);
        }
        break;
    case ConsoleAction::Sync:
#if HAZARD_ENABLE_LORA
    {
        const HazardSyncStats &s = hazardSync->stats();
        commandOutput->printf("sync sent=%lu received=%lu imported=%lu unchanged=%lu conflicts=%lu rejected=%lu "
                      "malformed=%lu ignored=%lu retries=%lu exhausted=%lu queue_full=%lu busy=%lu "
                      "evidence_imported=%lu evidence_unchanged=%lu retirement_pages_imported=%lu\n",
            static_cast<unsigned long>(s.sent), static_cast<unsigned long>(s.received),
            static_cast<unsigned long>(s.imported), static_cast<unsigned long>(s.unchanged),
            static_cast<unsigned long>(s.conflicts), static_cast<unsigned long>(s.rejected),
            static_cast<unsigned long>(s.malformed), static_cast<unsigned long>(s.ignored),
            static_cast<unsigned long>(s.retries), static_cast<unsigned long>(s.exhausted),
            static_cast<unsigned long>(s.queueFull), static_cast<unsigned long>(s.transportBusy),
            static_cast<unsigned long>(s.evidenceImported), static_cast<unsigned long>(s.evidenceUnchanged),
            static_cast<unsigned long>(s.retirementPagesImported));
        break;
    }
#else
        commandOutput->println("SYNC_DISABLED_IN_SERIAL_BUILD");
        break;
#endif
    case ConsoleAction::Help: help(); break;
    case ConsoleAction::Time:
        if (phoneClock.set(c.utc, millis()))
        {
            commandOutput->println("TIME_OK");
            runLifecycle(phoneClock.now(millis()), true);
        }
        else commandOutput->println("INVALID_TIME_OR_CLOCK_ROLLBACK");
        break;
    case ConsoleAction::Report:
    {
        HazardId id;
        const HazardResult result = service->create(c.report, now, id, c.clientReportId);
        commandOutput->println(hazardResultName(result));
        if (result == HazardResult::Ok || result == HazardResult::Unchanged)
        {
            const HazardRecord *record = repository.findById(id);
            if (record) printRecord(*record);
            else commandOutput->printf("original_hazard=%llu record_pending\n", static_cast<unsigned long long>(id));
        }
        break;
    }
    case ConsoleAction::Revise:
    {
        const HazardResult result = service->revise(c.id, c.report, now, c.clientReportId);
        commandOutput->println(hazardResultName(result));
        if (result == HazardResult::Ok) printRecord(*repository.findById(c.id));
        break;
    }
    case ConsoleAction::Get:
    {
        const HazardRecord *record = repository.findById(c.id);
        if (record) printRecord(*record);
        else if (repository.findRetirement(c.id)) commandOutput->println("TERMINAL_RECORD_RETIRED; enter RETIRED");
        else commandOutput->println("NOT_FOUND");
        break;
    }
    case ConsoleAction::Resolve:
    {
        const HazardResult result = service->resolve(c.id, c.utc, now, c.clientReportId);
        commandOutput->println(hazardResultName(result));
        if (result == HazardResult::Ok) printRecord(*repository.findById(c.id));
        break;
    }
    case ConsoleAction::List:
    case ConsoleAction::Type:
    case ConsoleAction::Near:
    {
        size_t matches = 0;
        for (size_t i = 0; i < repository.count(); ++i)
        {
            HazardRecord record;
            repository.getAt(i, record);
            if (c.action == ConsoleAction::Type && record.type != c.type) continue;
            if (c.action == ConsoleAction::Near && hazardDistanceMeters(c.latitude, c.longitude,
                record.latitude, record.longitude) > c.radiusMeters) continue;
            printRecord(record);
            ++matches;
        }
        commandOutput->printf("matches=%u\n", static_cast<unsigned>(matches));
        break;
    }
    case ConsoleAction::Position:
        commandOutput->println(service->setPosition(c.latitude, c.longitude) ? "OK" : "INVALID_REPORT");
        break;
    case ConsoleAction::Cleanup:
    {
        size_t removed = 0;
        const HazardResult result = service->cleanupMemory(now, removed);
        commandOutput->printf("cleanup=%s removed=%u\n", hazardResultName(result), static_cast<unsigned>(removed));
        break;
    }
    case ConsoleAction::Retired:
        for (size_t i = 0; i < repository.retirementCount(); ++i)
        {
            HazardRetirement marker; repository.retirementAt(i, marker);
            commandOutput->printf("retired=%llu created=%lu status=%u(%s)\n", static_cast<unsigned long long>(marker.id),
                static_cast<unsigned long>(marker.createdAt), static_cast<unsigned>(marker.status), HazardLifecycle::statusName(marker.status));
        }
        break;
    case ConsoleAction::Memory:
        commandOutput->printf("records=%u capacity=%u record_bytes=%u repository_bytes=%u free_heap=%u utc=%lu "
                      "storage_writable=%u snapshot_generation=%llu\n",
            static_cast<unsigned>(repository.count()), static_cast<unsigned>(HazardRepository::CAPACITY),
            static_cast<unsigned>(sizeof(HazardRecord)), static_cast<unsigned>(sizeof(repository)),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned long>(now),
            static_cast<unsigned>(storage.writable()), static_cast<unsigned long long>(storage.generation()));
        commandOutput->printf("observations=%u evidence_capacity=%u max_snapshot_bytes=%u "
                              "schema=%u sync_protocol=%u confidence_policy=1 conflict_policy=%u\n",
            static_cast<unsigned>(repository.observationCount()), static_cast<unsigned>(HazardRepository::EVIDENCE_CAPACITY),
            static_cast<unsigned>(HazardSnapshot::MAX_SIZE), static_cast<unsigned>(HazardSnapshot::SCHEMA_VERSION),
            static_cast<unsigned>(HazardSyncCodec::PROTOCOL_VERSION), static_cast<unsigned>(HazardConflict::POLICY_VERSION));
        commandOutput->printf("memory_policy=1 retired_count=%u retirement_capacity=%u retention_seconds=%lu "
                              "evicted=%lu cleaned=%lu position_known=%u retirement_full=%u\n",
            static_cast<unsigned>(repository.retirementCount()), static_cast<unsigned>(HazardRepository::RETIREMENT_CAPACITY),
            static_cast<unsigned long>(HazardMemory::RETENTION_SECONDS), static_cast<unsigned long>(service->evictedCount()),
            static_cast<unsigned long>(service->cleanedCount()), static_cast<unsigned>(service->positionKnown()),
            static_cast<unsigned>(repository.retirementCount() == HazardRepository::RETIREMENT_CAPACITY));
        break;
    }
}

void executeWireless(const char *input, Print &output)
{
    char command[192];
    const size_t length = strlen(input);
    if (length == 0 || length >= sizeof(command)) { output.println("INVALID_COMMAND"); return; }
    memcpy(command, input, length + 1);
    commandOutput = &output;
    execute(command);
    commandOutput = &Serial;
}
}

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);
    identityReady = identity.begin("hazard-id", false);
    const uint64_t chipId = ESP.getEfuseMac();
    const uint32_t hardwareNode = static_cast<uint32_t>(chipId ^ (chipId >> 32));
    // Use the factory-derived identity unless an explicit unique ID is configured.
    const uint32_t node = HAZARD_NODE_ID ? HAZARD_NODE_ID : (hardwareNode ? hardwareNode : 1);
    uint32_t sequence = 0;
    if (identityReady)
    {
        if (identity.isKey("node"))
        {
            // Changing identity must not reset a counter or reuse an old node's IDs.
            identityReady = identity.getUInt("node", 0) == node && identity.isKey("sequence");
        }
        else
        {
            // Write the counter first so an interrupted initialization cannot expose a missing counter.
            identityReady = identity.putUInt("sequence", 0) == sizeof(uint32_t) &&
                            identity.putUInt("node", node) == sizeof(uint32_t);
        }
        if (identityReady) sequence = identity.getUInt("sequence", UINT32_MAX);
    }
    const bool mounted = storageBackend.begin();
    const StorageLoad loaded = storage.load(repository);
    // Fail closed if NVS has fallen behind records issued by this node.
    for (size_t i = 0; i < repository.count(); ++i)
    {
        HazardRecord record;
        repository.getAt(i, record);
        if (record.originNodeId == node && static_cast<uint32_t>(record.id) > sequence)
            identityReady = false;
    }
    for (size_t i = 0; i < repository.observationCount(); ++i)
    {
        HazardObservation observation; repository.observationAt(i, observation);
        if (observation.observerNodeId == node && static_cast<uint32_t>(observation.id) > sequence)
            identityReady = false;
    }
    for (size_t i = 0; i < repository.retirementCount(); ++i)
    {
        HazardRetirement marker; repository.retirementAt(i, marker);
        if ((marker.id >> 32) == node && static_cast<uint32_t>(marker.id) > sequence) identityReady = false;
    }
    service = new (serviceStorage) HazardService(repository, node, sequence, reserveSequence, nullptr, &storage);
    commandOutput->printf("Hazard memory stage 5; node=%lu; ID storage=%s\n",
                  static_cast<unsigned long>(node), identityReady ? "ready" : "unavailable (creation disabled)");
    commandOutput->printf("LittleFS=%s snapshot=%s records=%u; UTC remains unknown until TIME\n",
                  mounted ? "mounted" : "mount failed (not formatted)", storageLoadName(loaded),
                  static_cast<unsigned>(repository.count()));
#if HAZARD_ENABLE_LORA
    LoRa_Initialization();
    uint32_t session = esp_random();
    if (!session) session = 1;
    hazardSync = new (syncStorage) HazardSync(repository, *service, node, session, sendHazardFrame, nullptr);
    if (!LoRaTransport::addHandler(receiveHazardFrame, nullptr))
        commandOutput->println("SYNC_HANDLER_REGISTRATION_FAILED");
    LoRaTransport::begin();
#endif
    help();
    WiFiConsole::begin(node, executeWireless);
}

void loop()
{
    const uint32_t tick = millis();
    const uint32_t now = phoneClock.now(tick); // Includes millis() rollover.
    if (static_cast<uint32_t>(tick - lastLifecycleTick) >= 1000)
    {
        lastLifecycleTick = tick;
        runLifecycle(now);
    }
#if HAZARD_ENABLE_LORA
    LoRaTransport::poll();
    hazardSync->tick(millis());
#endif
    WiFiConsole::poll();
    size_t budget = 64;
    while (budget-- && Serial.available())
    {
        const int value = Serial.read();
        if (value < 0) break;
        const char ch = static_cast<char>(value);
        if (ch == '\n')
        {
            if (droppingLine) commandOutput->println("COMMAND_TOO_LONG_OR_INVALID");
            else if (lineLength) { line[lineLength] = 0; execute(line); }
            lineLength = 0;
            droppingLine = false;
        }
        else if (ch != '\r' && !droppingLine)
        {
            if (ch == 0 || lineLength == sizeof(line) - 1) droppingLine = true;
            else line[lineLength++] = ch;
        }
    }
}
