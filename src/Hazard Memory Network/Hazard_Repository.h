#pragma once

#include <stddef.h>
#include "Hazard_Record.h"
#include "Hazard_Evidence.h"

// Compact permanent terminal identity; never silently overwritten when full.
struct HazardRetirement
{
    HazardId id;
    uint32_t createdAt;
    HazardStatus status;
};
bool validRetirement(const HazardRetirement &marker);

class HazardRepository
{
public:
    static constexpr size_t CAPACITY = 16;
    static constexpr size_t EVIDENCE_CAPACITY = 32;
    static constexpr size_t RETIREMENT_CAPACITY = 64;
    size_t retirementCount() const { return retiredCount; }
    const HazardRetirement *findRetirement(HazardId id) const;
    bool retirementAt(size_t index, HazardRetirement &marker) const;
    bool retire(const HazardRetirement &marker);
    size_t removeEvidence(HazardId hazardId);
    bool sameContents(const HazardRepository &other) const;

    HazardRepository();

    bool add(const HazardRecord &record);
    const HazardRecord *findById(HazardId id) const;
    bool update(const HazardRecord &record);
    // Remote-only deterministic merge; local updates retain strict revision checks.
    bool mergeRemote(const HazardRecord &record);
    bool getAt(size_t index, HazardRecord &record) const;
    size_t count() const;
    size_t observationCount() const { return evidenceCount; }
    const HazardObservation *findObservation(uint64_t id) const;
    const HazardObservation *findClientObservation(uint32_t observer, uint64_t clientId) const;
    bool observationAt(size_t index, HazardObservation &observation) const;
    bool addObservation(const HazardObservation &observation);
    bool remove(HazardId id);
    // Returns total matches; copies up to outputCapacity into caller-owned storage.
    size_t findByType(HazardType type, HazardRecord *output, size_t outputCapacity) const;
    size_t findNearby(double latitude, double longitude, double radiusMeters,
                      HazardRecord *output, size_t outputCapacity) const;

private:
    HazardRecord records[CAPACITY];
    size_t recordCount;
    HazardObservation evidence[EVIDENCE_CAPACITY];
    size_t evidenceCount;
    HazardRetirement retired[RETIREMENT_CAPACITY];
    size_t retiredCount;
};
