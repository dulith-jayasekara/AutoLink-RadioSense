#pragma once

#include <stddef.h>
#include "Hazard_Record.h"
#include "Hazard_Evidence.h"

class HazardRepository
{
public:
    static constexpr size_t CAPACITY = 16;
    static constexpr size_t EVIDENCE_CAPACITY = 32;

    HazardRepository();

    bool add(const HazardRecord &record);
    const HazardRecord *findById(HazardId id) const;
    bool update(const HazardRecord &record);
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
};
