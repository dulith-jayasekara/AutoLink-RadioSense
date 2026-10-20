#pragma once
#include "Hazard_Repository.h"

class HazardMemory
{
public:
    static constexpr unsigned POLICY_VERSION = 1;
    static constexpr uint32_t RETENTION_SECONDS = 86400;
    static constexpr uint32_t RETRY_PROTECTION_SECONDS = 300;
    // Position is optional application-supplied context; never a fabricated GPS fix.
    static unsigned priority(const HazardRepository &repo, const HazardRecord &record, uint32_t now,
                             bool positionKnown = false, double latitude = 0, double longitude = 0);
    static size_t cleanup(HazardRepository &repo, uint32_t now);
    // May discard lower-priority ongoing cache entries plus their linked evidence.
    // Terminal entries and recent local keyed observations are pinned.
    static bool makeRoom(HazardRepository &repo, const HazardRecord &incoming, uint32_t now,
                         uint32_t localNode, bool needsRecord, bool needsEvidence,
                         bool positionKnown = false, double latitude = 0, double longitude = 0);
};
