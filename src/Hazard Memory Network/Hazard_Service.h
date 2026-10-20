#pragma once
#include "Hazard_Repository.h"
class HazardStorage;

// Transport-independent phone input; confidence/version/ID belong to the ESP32.
struct PhoneHazardReport
{
    HazardType type;
    uint8_t severity;
    double latitude;
    double longitude;
    uint32_t observedAt;
    uint32_t locationFixAt;
    float locationAccuracyMeters;
};

enum class HazardResult : uint8_t
{
    Ok, InvalidReport, ClockUnavailable, Full, NotFound, StaleUpdate,
    TerminalRecord, SequenceExhausted, PersistenceFailed, RecordPersistenceFailed, VersionExhausted,
    Unchanged, Conflict, EvidenceFull
};
const char *hazardResultName(HazardResult result);

class HazardService
{
public:
    // Must durably reserve the sequence before a record becomes visible.
    using ReserveSequence = bool (*)(uint32_t sequence, void *context);
    HazardService(HazardRepository &repository, uint32_t nodeId,
                  uint32_t lastSequence, ReserveSequence reserve, void *context,
                  HazardStorage *storage = nullptr);
    HazardResult create(const PhoneHazardReport &report, uint32_t now, HazardId &id, uint64_t clientId = 0);
    HazardResult revise(HazardId id, const PhoneHazardReport &report, uint32_t now, uint64_t clientId = 0);
    HazardResult resolve(HazardId id, uint32_t clearanceAt, uint32_t now, uint64_t clientId = 0);
    // Immutable presence votes; do not edit or reopen the canonical hazard record.
    HazardResult assess(HazardId id, const PhoneHazardReport &report, ObservationKind kind,
                        uint32_t now, uint64_t clientId);
    // One atomic persistent batch; changed is zero unless the entire batch commits.
    HazardResult processLifecycle(uint32_t now, size_t &changed);
    // Preserve remote identity, observation and confidence; persist before acceptance.
    HazardResult importRemote(const HazardRecord &record);
    HazardResult importObservation(const HazardObservation &observation);
    HazardResult importRetirements(const HazardRetirement *markers, size_t count);
    HazardResult cleanupMemory(uint32_t now, size_t &removed);
    bool setPosition(double latitude, double longitude);
    bool positionKnown() const { return hasPosition; }
    unsigned priority(const HazardRecord &record, uint32_t now) const;
    uint32_t evictedCount() const { return memoryEvictions; }
    uint32_t cleanedCount() const { return memoryCleaned; }
    static uint32_t ttlSeconds(HazardType type);
    static constexpr uint8_t INITIAL_CONFIDENCE = 50;
    static constexpr uint32_t MAX_FIX_AGE_SECONDS = 120;
    static constexpr uint32_t MAX_REPORT_AGE_SECONDS = 300;
    static constexpr float MAX_ACCURACY_METERS = 100.0f;

private:
    HazardResult validate(const PhoneHazardReport &report, uint32_t now) const;
    void applyReport(HazardRecord &record, const PhoneHazardReport &report) const;
    HazardResult commit(const HazardRepository &candidate);
    HazardResult allocateId(HazardId &id);
    HazardObservation observation(const PhoneHazardReport &report, HazardId hazardId,
                                  ObservationKind kind, uint64_t clientId) const;
    HazardRepository &repository;
    uint32_t nodeId;
    uint32_t lastSequence;
    ReserveSequence reserve;
    void *context;
    HazardStorage *storage;
    uint32_t memoryUtc, memoryEvictions, memoryCleaned;
    bool hasPosition;
    double positionLatitude, positionLongitude;
};
