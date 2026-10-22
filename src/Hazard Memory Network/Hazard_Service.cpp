#include "Hazard_Service.h"
#include "Hazard_Storage.h"
#include "Hazard_Lifecycle.h"
#include "Hazard_Confidence.h"
#include "Hazard_Conflict.h"
#include "Hazard_Memory.h"
#include <cmath>

const char *hazardResultName(HazardResult result)
{
    switch (result)
    {
    case HazardResult::Ok: return "OK";
    case HazardResult::InvalidReport: return "INVALID_REPORT";
    case HazardResult::ClockUnavailable: return "CLOCK_UNAVAILABLE";
    case HazardResult::Full: return "REPOSITORY_FULL";
    case HazardResult::NotFound: return "NOT_FOUND";
    case HazardResult::StaleUpdate: return "STALE_UPDATE";
    case HazardResult::TerminalRecord: return "TERMINAL_RECORD";
    case HazardResult::SequenceExhausted: return "SEQUENCE_EXHAUSTED";
    case HazardResult::PersistenceFailed: return "ID_PERSISTENCE_FAILED";
    case HazardResult::RecordPersistenceFailed: return "RECORD_PERSISTENCE_FAILED";
    case HazardResult::VersionExhausted: return "VERSION_EXHAUSTED";
    case HazardResult::Unchanged: return "UNCHANGED";
    case HazardResult::Conflict: return "CONFLICT";
    case HazardResult::EvidenceFull: return "EVIDENCE_FULL";
    }
    return "UNKNOWN";
}

HazardService::HazardService(HazardRepository &repo, uint32_t node,
                             uint32_t sequence, ReserveSequence callback, void *data,
                             HazardStorage *persistence)
    : repository(repo), nodeId(node), lastSequence(sequence), reserve(callback), context(data),
      storage(persistence), memoryUtc(0), memoryEvictions(0), memoryCleaned(0), recordReplayBlocks(0), evidenceReplayBlocks(0),
      hasPosition(false), positionLatitude(0), positionLongitude(0) {}

HazardResult HazardService::commit(const HazardRepository &candidate)
{
    if (repository.sameContents(candidate)) return HazardResult::Ok;
    if (storage && !storage->save(candidate)) return HazardResult::RecordPersistenceFailed;
    for (size_t i = 0; i < repository.count(); ++i)
    {
        HazardRecord r; repository.getAt(i, r);
        if (!candidate.findById(r.id))
        {
            if (candidate.findRetirement(r.id)) ++memoryCleaned;
            else ++memoryEvictions;
        }
    }
    repository = candidate;
    return HazardResult::Ok;
}

HazardResult HazardService::importRemote(const HazardRecord &record)
{
    if (!isValidHazardRecord(record)) return HazardResult::InvalidReport;
    if (record.originNodeId == nodeId && static_cast<uint32_t>(record.id) > lastSequence)
        return HazardResult::PersistenceFailed;
    const HazardRetirement *retired = repository.findRetirement(record.id);
    if (retired)
    {
        if (retired->createdAt != record.createdAt) return HazardResult::Conflict;
        // A clearance dominates timeout even after history was compacted. Keep
        // only its terminal identity; never restore its payload or observations.
        if (retired->status == HazardStatus::Expired && record.status == HazardStatus::Resolved)
        {
            const HazardRetirement resolved{record.id, record.createdAt, HazardStatus::Resolved};
            return importRetirements(&resolved, 1);
        }
        ++recordReplayBlocks;
        return HazardResult::TerminalRecord;
    }
    const HazardRecord *current = repository.findById(record.id);
    if (current)
    {
        if (sameHazardRecord(*current, record)) return HazardResult::Unchanged;
        if (!HazardConflict::sameIdentity(*current, record)) return HazardResult::Conflict;
        if (HazardConflict::compare(record, *current) <= 0)
        {
            if ((current->status == HazardStatus::Resolved || current->status == HazardStatus::Expired) &&
                record.status != current->status) return HazardResult::TerminalRecord;
            return HazardResult::StaleUpdate;
        }
    }
    HazardRepository candidate = repository;
    HazardMemory::cleanup(candidate, memoryUtc);
    if (candidate.findRetirement(record.id)) return HazardResult::TerminalRecord;
    // Old remote payloads can be retired without first writing their full history.
    if (memoryUtc >= record.expiresAt && memoryUtc - record.expiresAt >= HazardMemory::RETENTION_SECONDS)
    {
        const HazardStatus state = record.status == HazardStatus::Resolved ? HazardStatus::Resolved : HazardStatus::Expired;
        if (!candidate.retire(HazardRetirement{record.id, record.createdAt, state})) return HazardResult::Full;
        return commit(candidate);
    }
    if (!current && !HazardMemory::makeRoom(candidate, record, memoryUtc, nodeId, true, false,
        hasPosition, positionLatitude, positionLongitude)) return HazardResult::Full;
    if (!candidate.mergeRemote(record)) return HazardResult::Conflict;
    return commit(candidate);
}

uint32_t HazardService::ttlSeconds(HazardType type)
{
    // Prototype values, to be calibrated during evaluation.
    switch (type)
    {
    case HazardType::RoadDamage: return 86400;
    case HazardType::Flooding: return 7200;
    case HazardType::Accident: return 3600;
    default: return 3600;
    }
}

HazardResult HazardService::validate(const PhoneHazardReport &r, uint32_t now) const
{
    if (now == 0) return HazardResult::ClockUnavailable;
    if (nodeId == 0 || !isValidHazardType(r.type) || r.severity < 1 || r.severity > 5 ||
        !isValidCoordinates(r.latitude, r.longitude) || r.observedAt == 0 ||
        r.observedAt > now || now - r.observedAt > MAX_REPORT_AGE_SECONDS ||
        r.locationFixAt == 0 || r.locationFixAt > r.observedAt ||
        r.observedAt - r.locationFixAt > MAX_FIX_AGE_SECONDS ||
        !std::isfinite(r.locationAccuracyMeters) || r.locationAccuracyMeters <= 0 ||
        r.locationAccuracyMeters > MAX_ACCURACY_METERS ||
        r.observedAt > UINT32_MAX - ttlSeconds(r.type))
        return HazardResult::InvalidReport;
    return HazardResult::Ok;
}

void HazardService::applyReport(HazardRecord &r, const PhoneHazardReport &input) const
{
    r.updatedByNodeId = nodeId;
    r.type = input.type;
    r.severity = input.severity;
    r.latitude = input.latitude;
    r.longitude = input.longitude;
    r.lastObservedAt = input.observedAt;
    r.locationFixAt = input.locationFixAt;
    r.locationAccuracyMeters = input.locationAccuracyMeters;
    r.expiresAt = input.observedAt + ttlSeconds(input.type);
}

HazardResult HazardService::allocateId(HazardId &id)
{
    id = 0;
    if (!nodeId) return HazardResult::InvalidReport;
    if (lastSequence == UINT32_MAX) return HazardResult::SequenceExhausted;
    const uint32_t next = lastSequence + 1;
    const HazardId candidate = (static_cast<HazardId>(nodeId) << 32) | next;
    if (repository.findById(candidate) || repository.findObservation(candidate) || repository.findRetirement(candidate)) return HazardResult::PersistenceFailed;
    if (!reserve || !reserve(next, context)) return HazardResult::PersistenceFailed;
    lastSequence = next;
    id = candidate;
    return HazardResult::Ok;
}

HazardObservation HazardService::observation(const PhoneHazardReport &input, HazardId hazardId,
                                             ObservationKind kind, uint64_t clientId) const
{
    HazardObservation o = {};
    o.observerNodeId = nodeId; o.hazardId = hazardId; o.kind = kind; o.clientReportId = clientId;
    o.type = input.type; o.severity = input.severity; o.latitude = input.latitude; o.longitude = input.longitude;
    o.observedAt = input.observedAt; o.locationFixAt = input.locationFixAt;
    o.locationAccuracyMeters = input.locationAccuracyMeters;
    return o;
}

HazardResult HazardService::importObservation(const HazardObservation &o)
{
    if (!HazardEvidenceCodec::valid(o)) return HazardResult::InvalidReport;
    if (repository.findRetirement(o.hazardId)) { ++evidenceReplayBlocks; return HazardResult::TerminalRecord; }
    const HazardObservation *current = repository.findObservation(o.id);
    if (current) return sameObservation(*current, o) ? HazardResult::Unchanged : HazardResult::Conflict;
    if (repository.findClientObservation(o.observerNodeId, o.clientReportId)) return HazardResult::Conflict;
    if (o.observerNodeId == nodeId && static_cast<uint32_t>(o.id) > lastSequence)
        return HazardResult::PersistenceFailed;
    HazardRepository candidate = repository;
    HazardMemory::cleanup(candidate, memoryUtc);
    if (candidate.findRetirement(o.hazardId)) return HazardResult::TerminalRecord;
    if (candidate.observationCount() == HazardRepository::EVIDENCE_CAPACITY)
    {
        const HazardRecord *target = candidate.findById(o.hazardId);
        if (!target || !HazardMemory::makeRoom(candidate, *target, memoryUtc, nodeId, false, true,
            hasPosition, positionLatitude, positionLongitude)) return HazardResult::EvidenceFull;
    }
    if (!candidate.addObservation(o)) return HazardResult::InvalidReport;
    return commit(candidate);
}

HazardResult HazardService::create(const PhoneHazardReport &input, uint32_t now, HazardId &id, uint64_t clientId)
{
    id = 0;
    HazardObservation evidence = observation(input, 0, ObservationKind::Report, clientId);
    const HazardObservation *previous = repository.findClientObservation(nodeId, clientId);
    if (previous)
    {
        evidence.hazardId = previous->hazardId;
        if (!sameObservationPayload(*previous, evidence)) return HazardResult::Conflict;
        id = previous->hazardId;
        return HazardResult::Unchanged;
    }
    const HazardResult result = validate(input, now);
    if (result != HazardResult::Ok) return result;
    if (storage && !storage->writable()) return HazardResult::RecordPersistenceFailed;
    memoryUtc = now;
    HazardRecord record = {};
    record.id = (static_cast<HazardId>(nodeId) << 32) | (lastSequence == UINT32_MAX ? UINT32_MAX : lastSequence + 1);
    record.originNodeId = nodeId;
    record.createdAt = input.observedAt;
    record.version = 1;
    record.confidence = INITIAL_CONFIDENCE;
    record.status = HazardStatus::Unverified;
    applyReport(record, input);
    HazardRepository candidateRepository = repository;
    HazardMemory::cleanup(candidateRepository, now);
    if (!HazardMemory::makeRoom(candidateRepository, record, now, nodeId, true, true,
        hasPosition, positionLatitude, positionLongitude))
        return candidateRepository.count() == HazardRepository::CAPACITY ? HazardResult::Full : HazardResult::EvidenceFull;
    HazardId candidate;
    const HazardResult allocated = allocateId(candidate);
    if (allocated != HazardResult::Ok) return allocated;
    record.id = candidate;
    if (!candidateRepository.add(record)) return HazardResult::InvalidReport;
    evidence.id = evidence.hazardId = candidate;
    if (!candidateRepository.addObservation(evidence)) return HazardResult::Conflict;
    const HazardResult committed = commit(candidateRepository);
    if (committed != HazardResult::Ok) return committed;
    id = candidate;
    return HazardResult::Ok;
}

HazardResult HazardService::revise(HazardId id, const PhoneHazardReport &input, uint32_t now, uint64_t clientId)
{
    HazardObservation evidence = observation(input, id, ObservationKind::Revision, clientId);
    const HazardObservation *previous = repository.findClientObservation(nodeId, clientId);
    if (previous) return sameObservationPayload(*previous, evidence) ? HazardResult::Unchanged : HazardResult::Conflict;
    const HazardResult result = validate(input, now);
    if (result != HazardResult::Ok) return result;
    if (storage && !storage->writable()) return HazardResult::RecordPersistenceFailed;
    const HazardRecord *current = repository.findById(id);
    if (!current) return repository.findRetirement(id) ? HazardResult::TerminalRecord : HazardResult::NotFound;
    if (current->status == HazardStatus::Resolved || current->status == HazardStatus::Expired ||
        now >= current->expiresAt)
        return HazardResult::TerminalRecord;
    if (current->version == UINT32_MAX) return HazardResult::VersionExhausted;
    if (input.observedAt <= current->lastObservedAt)
        return HazardResult::StaleUpdate;
    memoryUtc = now;
    HazardRecord updated = *current;
    applyReport(updated, input);
    if (updated.status == HazardStatus::Stale) updated.status = HazardStatus::Unverified;
    updated.status = HazardLifecycle::evaluate(updated, now);
    ++updated.version;
    HazardRepository candidate = repository;
    HazardMemory::cleanup(candidate, now);
    if (!HazardMemory::makeRoom(candidate, updated, now, nodeId, false, true,
        hasPosition, positionLatitude, positionLongitude)) return HazardResult::EvidenceFull;
    HazardId observationId;
    const HazardResult allocated = allocateId(observationId);
    if (allocated != HazardResult::Ok) return allocated;
    evidence.id = observationId;
    if (!candidate.update(updated)) return HazardResult::StaleUpdate;
    if (!candidate.addObservation(evidence)) return HazardResult::Conflict;
    return commit(candidate);
}

HazardResult HazardService::resolve(HazardId id, uint32_t clearanceAt, uint32_t now, uint64_t clientId)
{
    const HazardObservation *previous = repository.findClientObservation(nodeId, clientId);
    if (previous)
        return previous->kind == ObservationKind::Clearance && previous->hazardId == id &&
            previous->observedAt == clearanceAt ? HazardResult::Unchanged : HazardResult::Conflict;
    if (!now) return HazardResult::ClockUnavailable;
    if (!nodeId || !clearanceAt || clearanceAt > now || now - clearanceAt > MAX_REPORT_AGE_SECONDS)
        return HazardResult::InvalidReport;
    const HazardRecord *current = repository.findById(id);
    if (!current) return repository.findRetirement(id) ? HazardResult::TerminalRecord : HazardResult::NotFound;
    if (clearanceAt < current->lastObservedAt) return HazardResult::StaleUpdate;
    if (current->status == HazardStatus::Resolved) return HazardResult::Ok; // Idempotent, no flash write.
    if (current->status == HazardStatus::Expired || now >= current->expiresAt)
        return HazardResult::TerminalRecord;
    if (current->version == UINT32_MAX) return HazardResult::VersionExhausted;
    if (storage && !storage->writable()) return HazardResult::RecordPersistenceFailed;
    const PhoneHazardReport clearance{current->type, current->severity, current->latitude, current->longitude,
        clearanceAt, current->locationFixAt, current->locationAccuracyMeters};
    HazardObservation evidence = observation(clearance, id, ObservationKind::Clearance, clientId);
    memoryUtc = now;
    HazardRepository candidate = repository;
    HazardMemory::cleanup(candidate, now);
    if (!HazardMemory::makeRoom(candidate, *current, now, nodeId, false, true,
        hasPosition, positionLatitude, positionLongitude)) return HazardResult::EvidenceFull;
    HazardId observationId;
    const HazardResult allocated = allocateId(observationId);
    if (allocated != HazardResult::Ok) return allocated;
    evidence.id = observationId;
    HazardRecord resolved = *current;
    resolved.status = HazardStatus::Resolved;
    resolved.updatedByNodeId = nodeId;
    ++resolved.version;
    if (!candidate.update(resolved)) return HazardResult::StaleUpdate;
    if (!candidate.addObservation(evidence)) return HazardResult::Conflict;
    return commit(candidate);
}

HazardResult HazardService::assess(HazardId id, const PhoneHazardReport &input, ObservationKind kind,
                                   uint32_t now, uint64_t clientId)
{
    if (!clientId || (kind != ObservationKind::Confirmation && kind != ObservationKind::Contradiction))
        return HazardResult::InvalidReport;
    HazardObservation evidence = observation(input, id, kind, clientId);
    const HazardObservation *previous = repository.findClientObservation(nodeId, clientId);
    if (previous) return sameObservationPayload(*previous, evidence) ? HazardResult::Unchanged : HazardResult::Conflict;
    const HazardResult valid = validate(input, now);
    if (valid != HazardResult::Ok) return valid;
    const HazardRecord *record = repository.findById(id);
    if (!record) return repository.findRetirement(id) ? HazardResult::TerminalRecord : HazardResult::NotFound;
    if (record->status == HazardStatus::Resolved || record->status == HazardStatus::Expired || now >= record->expiresAt)
        return HazardResult::TerminalRecord;
    if (input.observedAt < record->lastObservedAt) return HazardResult::StaleUpdate;
    if (input.type != record->type || input.locationAccuracyMeters > HazardConfidence::MAX_DISTANCE_METERS ||
        hazardDistanceMeters(input.latitude, input.longitude, record->latitude, record->longitude) >
            HazardConfidence::MAX_DISTANCE_METERS) return HazardResult::InvalidReport;
    if (storage && !storage->writable()) return HazardResult::RecordPersistenceFailed;
    memoryUtc = now;
    HazardRepository candidate = repository;
    HazardMemory::cleanup(candidate, now);
    if (!HazardMemory::makeRoom(candidate, *record, now, nodeId, false, true,
        hasPosition, positionLatitude, positionLongitude)) return HazardResult::EvidenceFull;
    const HazardResult allocated = allocateId(evidence.id);
    if (allocated != HazardResult::Ok) return allocated;
    if (!candidate.addObservation(evidence)) return HazardResult::Conflict;
    return commit(candidate);
}

HazardResult HazardService::processLifecycle(uint32_t now, size_t &changed)
{
    changed = 0;
    memoryUtc = now;
    if (!now) return HazardResult::ClockUnavailable;
    if (!nodeId) return HazardResult::InvalidReport;
    HazardRepository candidate = repository;
    size_t pending = 0;
    for (size_t i = 0; i < repository.count(); ++i)
    {
        HazardRecord record;
        repository.getAt(i, record);
        const HazardStatus next = HazardLifecycle::evaluate(record, now);
        if (next == record.status) continue;
        if (record.version == UINT32_MAX) return HazardResult::VersionExhausted;
        record.status = next;
        record.updatedByNodeId = nodeId;
        ++record.version;
        if (!candidate.update(record)) return HazardResult::StaleUpdate;
        ++pending;
    }
    const size_t removed = HazardMemory::cleanup(candidate, now);
    if (!pending && !removed) return HazardResult::Ok; // No repeated writes for unchanged states.
    const HazardResult result = commit(candidate);
    if (result == HazardResult::Ok) changed = pending;
    return result;
}

bool HazardService::setPosition(double latitude, double longitude)
{
    if (!isValidCoordinates(latitude, longitude)) return false;
    positionLatitude = latitude; positionLongitude = longitude; hasPosition = true; return true;
}
unsigned HazardService::priority(const HazardRecord &record, uint32_t now) const
{
    return HazardMemory::priority(repository, record, now, hasPosition, positionLatitude, positionLongitude);
}
HazardResult HazardService::cleanupMemory(uint32_t now, size_t &removed)
{
    removed = 0; memoryUtc = now;
    if (!now) return HazardResult::ClockUnavailable;
    HazardRepository candidate = repository;
    const size_t count = HazardMemory::cleanup(candidate, now);
    if (!count) return HazardResult::Ok;
    const HazardResult result = commit(candidate);
    if (result == HazardResult::Ok) removed = count;
    return result;
}
HazardResult HazardService::importRetirements(const HazardRetirement *markers, size_t count)
{
    if (!markers || !count || count > 4) return HazardResult::InvalidReport;
    HazardRepository candidate = repository;
    for (size_t i = 0; i < count; ++i)
    {
        const HazardRetirement &marker = markers[i];
        if (!validRetirement(marker)) return HazardResult::InvalidReport;
        if ((marker.id >> 32) == nodeId && static_cast<uint32_t>(marker.id) > lastSequence)
            return HazardResult::PersistenceFailed;
        const HazardRetirement *existing = candidate.findRetirement(marker.id);
        const HazardRecord *record = candidate.findById(marker.id);
        if ((existing && existing->createdAt != marker.createdAt) ||
            (record && record->createdAt != marker.createdAt)) return HazardResult::Conflict;
        if (!candidate.retire(marker)) return HazardResult::Full;
    }
    if (repository.sameContents(candidate)) return HazardResult::Unchanged;
    return commit(candidate);
}
