#pragma once
#include "Hazard_Repository.h"

struct HazardConfidenceResult
{
    uint8_t score, evidenceScore, decayPenalty;
    size_t supportingObservers, contradictingObservers;
    bool timeKnown;
    HazardStatus effectiveStatus;
};

// A derived presence score, not a probability or a replicated record revision.
class HazardConfidence
{
public:
    static constexpr uint8_t INITIAL_SCORE = 50;
    static constexpr uint8_t SUPPORT_WEIGHT = 20;
    static constexpr uint8_t CONTRADICTION_WEIGHT = 25;
    static constexpr uint8_t DECAY_PER_QUARTER = 10;
    static constexpr double MAX_DISTANCE_METERS = 25;
    static constexpr uint32_t REPORT_WINDOW_SECONDS = 120;
    static HazardConfidenceResult evaluate(const HazardRepository &repository,
                                          const HazardRecord &record, uint32_t now);
    static const char *level(uint8_t score);
};
