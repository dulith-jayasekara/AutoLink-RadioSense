#include "Hazard_Lifecycle.h"

bool HazardLifecycle::ageSeconds(const HazardRecord &record, uint32_t now, uint32_t &age)
{
    age = 0;
    if (!now || now < record.lastObservedAt) return false;
    age = now - record.lastObservedAt;
    return true;
}

HazardStatus HazardLifecycle::evaluate(const HazardRecord &record, uint32_t now)
{
    // Resolution/expiry markers are terminal even if clocks or confidence change.
    if (record.status == HazardStatus::Resolved || record.status == HazardStatus::Expired)
        return record.status;
    uint32_t age;
    if (!ageSeconds(record, now, age)) return record.status;
    if (now >= record.expiresAt) return HazardStatus::Expired;
    // An earlier clock after reboot must not make an aged observation fresh again.
    // Only an accepted newer observation explicitly clears Stale in the service.
    if (record.status == HazardStatus::Stale) return HazardStatus::Stale;
    const uint32_t lifetime = record.expiresAt - record.lastObservedAt;
    const uint32_t staleAge = lifetime / 2 + lifetime % 2;
    if (age >= staleAge) return HazardStatus::Stale;
    return record.confidence >= ACTIVE_CONFIDENCE ? HazardStatus::Active : HazardStatus::Unverified;
}

const char *HazardLifecycle::statusName(HazardStatus status)
{
    switch (status)
    {
    case HazardStatus::Unverified: return "Unverified";
    case HazardStatus::Active: return "Active";
    case HazardStatus::Stale: return "Stale";
    case HazardStatus::Resolved: return "Resolved";
    case HazardStatus::Expired: return "Expired";
    }
    return "Unknown";
}
