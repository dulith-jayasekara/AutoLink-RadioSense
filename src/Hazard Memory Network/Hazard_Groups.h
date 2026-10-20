#pragma once
#include "Hazard_Repository.h"

struct HazardGroup
{
    HazardId canonicalId;
    HazardId members[HazardRepository::CAPACITY];
    size_t memberCount, observationCount, independentObservers;
    bool mixedTerminalStates;
};
class HazardGroups
{
public:
    static constexpr double MATCH_DISTANCE_METERS = 25;
    static constexpr uint32_t MATCH_TIME_SECONDS = 120;
    // Sorted, complete-link geographical grouping; raw records/evidence stay intact.
    static size_t build(const HazardRepository &repository, HazardGroup *output, size_t capacity);
    static bool matches(const HazardObservation &a, const HazardObservation &b);
};
