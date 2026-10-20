#pragma once
#include "Hazard_Record.h"

// Pure, clock-independent ordering for records with the same immutable identity.
// Positive means a wins, negative means b wins. No synthesized version or votes.
class HazardConflict
{
public:
    static constexpr unsigned POLICY_VERSION = 1;
    static bool sameIdentity(const HazardRecord &a, const HazardRecord &b);
    static int compare(const HazardRecord &a, const HazardRecord &b);
};
