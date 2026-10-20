#pragma once
#include "Hazard_Record.h"

class HazardLifecycle
{
public:
    // Prototype threshold; confidence evidence/scoring is implemented separately.
    static constexpr uint8_t ACTIVE_CONFIDENCE = 70;
    static bool ageSeconds(const HazardRecord &record, uint32_t now, uint32_t &age);
    static HazardStatus evaluate(const HazardRecord &record, uint32_t now);
    static const char *statusName(HazardStatus status);
};
