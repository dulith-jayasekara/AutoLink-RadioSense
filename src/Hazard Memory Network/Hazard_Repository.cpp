#include "Hazard_Repository.h"
#include "Hazard_Conflict.h"
#include <cmath>

HazardRepository::HazardRepository()
    : records{}, recordCount(0), evidence{}, evidenceCount(0), retired{}, retiredCount(0)
{
}

const HazardObservation *HazardRepository::findObservation(uint64_t id) const
{
    for (size_t i = 0; i < evidenceCount; ++i) if (evidence[i].id == id) return &evidence[i];
    return nullptr;
}
const HazardObservation *HazardRepository::findClientObservation(uint32_t observer, uint64_t clientId) const
{
    if (!clientId) return nullptr;
    for (size_t i = 0; i < evidenceCount; ++i)
        if (evidence[i].observerNodeId == observer && evidence[i].clientReportId == clientId) return &evidence[i];
    return nullptr;
}
bool HazardRepository::observationAt(size_t index, HazardObservation &out) const
{
    if (index >= evidenceCount) return false;
    out = evidence[index]; return true;
}
bool HazardRepository::addObservation(const HazardObservation &o)
{
    if (!HazardEvidenceCodec::valid(o) || evidenceCount == EVIDENCE_CAPACITY ||
        findRetirement(o.hazardId) || findObservation(o.id) || findClientObservation(o.observerNodeId, o.clientReportId)) return false;
    evidence[evidenceCount++] = o; return true;
}

bool HazardRepository::add(const HazardRecord &record)
{
    if (!isValidHazardRecord(record) ||
        recordCount >= CAPACITY || findRetirement(record.id) ||
        findById(record.id) != nullptr)
    {
        return false;
    }

    records[recordCount++] = record;
    return true;
}

const HazardRecord *HazardRepository::findById(HazardId id) const
{
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == id)
        {
            return &records[i];
        }
    }

    return nullptr;
}

bool HazardRepository::update(const HazardRecord &record)
{
    if (!isValidHazardRecord(record))
    {
        return false;
    }

    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == record.id)
        {
            const HazardRecord &current = records[i];
            // Local edits require a strictly newer, non-regressing revision.
            if (record.originNodeId != current.originNodeId ||
                record.createdAt != current.createdAt ||
                record.version <= current.version ||
                record.lastObservedAt < current.lastObservedAt ||
                ((current.status == HazardStatus::Resolved ||
                  current.status == HazardStatus::Expired) && record.status != current.status))
                return false;
            records[i] = record;
            return true;
        }
    }

    return false;
}

bool HazardRepository::mergeRemote(const HazardRecord &record)
{
    if (!isValidHazardRecord(record)) return false;
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id != record.id) continue;
        if (!HazardConflict::sameIdentity(records[i], record) ||
            HazardConflict::compare(record, records[i]) <= 0) return false;
        records[i] = record;
        return true;
    }
    return add(record);
}

bool HazardRepository::getAt(size_t index, HazardRecord &record) const
{
    if (index >= recordCount)
    {
        return false;
    }

    record = records[index];
    return true;
}

size_t HazardRepository::count() const
{
    return recordCount;
}

bool HazardRepository::remove(HazardId id)
{
    for (size_t i = 0; i < recordCount; ++i)
    {
        if (records[i].id == id)
        {
            for (size_t j = i + 1; j < recordCount; ++j)
            {
                records[j - 1] = records[j];
            }

            --recordCount;
            return true;
        }
    }

    return false;
}

size_t HazardRepository::findByType(HazardType type, HazardRecord *output, size_t limit) const
{
    size_t matches = 0;
    for (size_t i = 0; i < recordCount; ++i)
        if (records[i].type == type)
        {
            if (output && matches < limit) output[matches] = records[i];
            ++matches;
        }
    return matches;
}

size_t HazardRepository::findNearby(double lat, double lon, double radius,
                                    HazardRecord *output, size_t limit) const
{
    if (!isValidCoordinates(lat, lon) || !std::isfinite(radius) || radius < 0) return 0;
    size_t matches = 0;
    for (size_t i = 0; i < recordCount; ++i)
        if (hazardDistanceMeters(lat, lon, records[i].latitude, records[i].longitude) <= radius)
        {
            if (output && matches < limit) output[matches] = records[i];
            ++matches;
        }
    return matches;
}

bool validRetirement(const HazardRetirement &m)
{
    return (m.id >> 32) && static_cast<uint32_t>(m.id) && m.createdAt &&
        (m.status == HazardStatus::Resolved || m.status == HazardStatus::Expired);
}
const HazardRetirement *HazardRepository::findRetirement(HazardId id) const
{
    for (size_t i = 0; i < retiredCount; ++i) if (retired[i].id == id) return &retired[i];
    return nullptr;
}
bool HazardRepository::retirementAt(size_t index, HazardRetirement &marker) const
{
    if (index >= retiredCount) return false;
    marker = retired[index]; return true;
}
size_t HazardRepository::removeEvidence(HazardId id)
{
    size_t kept = 0, removed = 0;
    for (size_t i = 0; i < evidenceCount; ++i)
        if (evidence[i].hazardId == id) ++removed;
        else evidence[kept++] = evidence[i];
    evidenceCount = kept;
    return removed;
}
bool HazardRepository::retire(const HazardRetirement &marker)
{
    if (!validRetirement(marker)) return false;
    const HazardRecord *record = findById(marker.id);
    if (record && record->createdAt != marker.createdAt) return false;
    for (size_t i = 0; i < retiredCount; ++i)
        if (retired[i].id == marker.id)
        {
            if (retired[i].createdAt != marker.createdAt) return false;
            if (marker.status == HazardStatus::Resolved) retired[i].status = marker.status;
            remove(marker.id); removeEvidence(marker.id); return true;
        }
    if (retiredCount == RETIREMENT_CAPACITY) return false;
    retired[retiredCount] = marker;
    if (record && record->status == HazardStatus::Resolved) retired[retiredCount].status = HazardStatus::Resolved;
    ++retiredCount;
    remove(marker.id); removeEvidence(marker.id); return true;
}
bool HazardRepository::sameContents(const HazardRepository &other) const
{
    if (recordCount != other.recordCount || evidenceCount != other.evidenceCount ||
        retiredCount != other.retiredCount) return false;
    for (size_t i = 0; i < recordCount; ++i)
        if (!sameHazardRecord(records[i], other.records[i])) return false;
    for (size_t i = 0; i < evidenceCount; ++i)
        if (!sameObservation(evidence[i], other.evidence[i])) return false;
    for (size_t i = 0; i < retiredCount; ++i)
        if (retired[i].id != other.retired[i].id || retired[i].createdAt != other.retired[i].createdAt ||
            retired[i].status != other.retired[i].status) return false;
    return true;
}
