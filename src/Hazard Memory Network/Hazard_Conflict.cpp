#include "Hazard_Conflict.h"

namespace
{
unsigned terminalRank(HazardStatus status)
{
    if (status == HazardStatus::Resolved) return 2;
    if (status == HazardStatus::Expired) return 1;
    return 0;
}
template <typename T> int order(T a, T b) { return a > b ? 1 : (a < b ? -1 : 0); }
}
bool HazardConflict::sameIdentity(const HazardRecord &a, const HazardRecord &b)
{
    return a.id == b.id && a.originNodeId == b.originNodeId && a.createdAt == b.createdAt;
}
int HazardConflict::compare(const HazardRecord &a, const HazardRecord &b)
{
    // Both inputs must be valid and share identity. Terminal markers dominate
    // all ongoing revisions, including higher-version disconnected updates.
    int result = order(terminalRank(a.status), terminalRank(b.status));
    if (result) return result;
    result = order(a.lastObservedAt, b.lastObservedAt); if (result) return result;
    result = order(a.version, b.version); if (result) return result;
    result = order(a.status, b.status); if (result) return result;
    result = order(a.updatedByNodeId, b.updatedByNodeId); if (result) return result;
    // Explicit field ordering avoids padding, host byte order and hash collisions.
    // Remaining ties are mechanical, not additional independent evidence.
    result = order(a.type, b.type); if (result) return result;
    result = order(a.severity, b.severity); if (result) return result;
    result = order(a.latitude, b.latitude); if (result) return result;
    result = order(a.longitude, b.longitude); if (result) return result;
    result = order(a.locationFixAt, b.locationFixAt); if (result) return result;
    result = order(a.locationAccuracyMeters, b.locationAccuracyMeters); if (result) return result;
    result = order(a.expiresAt, b.expiresAt); if (result) return result;
    return order(a.confidence, b.confidence);
}
