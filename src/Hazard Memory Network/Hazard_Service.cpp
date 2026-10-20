#include "Hazard_Service.h"
#include "Hazard_Storage.h"
#include "Hazard_Lifecycle.h"
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
      storage(persistence) {}

HazardResult HazardService::commit(const HazardRepository &candidate)
{
    if (storage && !storage->save(candidate)) return HazardResult::RecordPersistenceFailed;
    repository = candidate;
    return HazardResult::Ok;
}

HazardResult HazardService::importRemote(const HazardRecord &record)
{
    if (!isValidHazardRecord(record)) return HazardResult::InvalidReport;
    if (record.originNodeId == nodeId && static_cast<uint32_t>(record.id) > lastSequence)
        return HazardResult::PersistenceFailed;
    const HazardRecord *current = repository.findById(record.id);
    if (current)
    {
        if (sameHazardRecord(*current, record)) return HazardResult::Unchanged;
        if (current->createdAt != record.createdAt) return HazardResult::Conflict;
        if ((current->status == HazardStatus::Resolved || current->status == HazardStatus::Expired) &&
            record.status != current->status) return HazardResult::TerminalRecord;
        if (record.version == current->version) return HazardResult::Conflict;
        if (record.version < current->version || record.lastObservedAt < current->lastObservedAt)
            return HazardResult::StaleUpdate;
    }
    else if (repository.count() == HazardRepository::CAPACITY) return HazardResult::Full;
    HazardRepository candidate = repository;
    if (!(current ? candidate.update(record) : candidate.add(record))) return HazardResult::Conflict;
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
    if (repository.findById(candidate) || repository.findObservation(candidate)) return HazardResult::PersistenceFailed;
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
    const HazardObservation *current = repository.findObservation(o.id);
    if (current) return sameObservation(*current, o) ? HazardResult::Unchanged : HazardResult::Conflict;
    if (repository.findClientObservation(o.observerNodeId, o.clientReportId)) return HazardResult::Conflict;
    if (o.observerNodeId == nodeId && static_cast<uint32_t>(o.id) > lastSequence)
        return HazardResult::PersistenceFailed;
    if (repository.observationCount() == HazardRepository::EVIDENCE_CAPACITY) return HazardResult::EvidenceFull;
    HazardRepository candidate = repository;
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
    if (repository.count() == HazardRepository::CAPACITY) return HazardResult::Full;
    if (repository.observationCount() == HazardRepository::EVIDENCE_CAPACITY) return HazardResult::EvidenceFull;
    HazardId candidate;
    const HazardResult allocated = allocateId(candidate);
    if (allocated != HazardResult::Ok) return allocated;
    HazardRecord record = {};
    record.id = candidate;
    record.originNodeId = nodeId;
    record.createdAt = input.observedAt;
    record.version = 1;
    record.confidence = INITIAL_CONFIDENCE;
    record.status = HazardStatus::Unverified;
    applyReport(record, input);
    HazardRepository candidateRepository = repository;
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
    if (!current) return HazardResult::NotFound;
    if (current->status == HazardStatus::Resolved || current->status == HazardStatus::Expired ||
        now >= current->expiresAt)
        return HazardResult::TerminalRecord;
    if (current->version == UINT32_MAX) return HazardResult::VersionExhausted;
    if (input.observedAt <= current->lastObservedAt)
        return HazardResult::StaleUpdate;
    if (repository.observationCount() == HazardRepository::EVIDENCE_CAPACITY) return HazardResult::EvidenceFull;
    HazardId observationId;
    const HazardResult allocated = allocateId(observationId);
    if (allocated != HazardResult::Ok) return allocated;
    evidence.id = observationId;
    HazardRecord updated = *current;
    applyReport(updated, input);
    if (updated.status == HazardStatus::Stale) updated.status = HazardStatus::Unverified;
    updated.status = HazardLifecycle::evaluate(updated, now);
    ++updated.version;
    HazardRepository candidate = repository;
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
    if (!current) return HazardResult::NotFound;
    if (clearanceAt < current->lastObservedAt) return HazardResult::StaleUpdate;
    if (current->status == HazardStatus::Resolved) return HazardResult::Ok; // Idempotent, no flash write.
    if (current->status == HazardStatus::Expired || now >= current->expiresAt)
        return HazardResult::TerminalRecord;
    if (current->version == UINT32_MAX) return HazardResult::VersionExhausted;
    if (repository.observationCount() == HazardRepository::EVIDENCE_CAPACITY) return HazardResult::EvidenceFull;
    if (storage && !storage->writable()) return HazardResult::RecordPersistenceFailed;
    const PhoneHazardReport clearance{current->type, current->severity, current->latitude, current->longitude,
        clearanceAt, current->locationFixAt, current->locationAccuracyMeters};
    HazardObservation evidence = observation(clearance, id, ObservationKind::Clearance, clientId);
    HazardId observationId;
    const HazardResult allocated = allocateId(observationId);
    if (allocated != HazardResult::Ok) return allocated;
    evidence.id = observationId;
    HazardRecord resolved = *current;
    resolved.status = HazardStatus::Resolved;
    resolved.updatedByNodeId = nodeId;
    ++resolved.version;
    HazardRepository candidate = repository;
    if (!candidate.update(resolved)) return HazardResult::StaleUpdate;
    if (!candidate.addObservation(evidence)) return HazardResult::Conflict;
    return commit(candidate);
}

HazardResult HazardService::processLifecycle(uint32_t now, size_t &changed)
{
    changed = 0;
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
    if (!pending) return HazardResult::Ok; // No repeated writes for unchanged states.
    const HazardResult result = commit(candidate);
    if (result == HazardResult::Ok) changed = pending;
    return result;
}
