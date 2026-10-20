#include "Hazard_Record.h"
#include <cmath>

bool sameHazardRecord(const HazardRecord &a, const HazardRecord &b)
{
    return a.id == b.id && a.originNodeId == b.originNodeId && a.updatedByNodeId == b.updatedByNodeId &&
        a.type == b.type && a.severity == b.severity && a.latitude == b.latitude && a.longitude == b.longitude &&
        a.createdAt == b.createdAt && a.lastObservedAt == b.lastObservedAt && a.locationFixAt == b.locationFixAt &&
        a.locationAccuracyMeters == b.locationAccuracyMeters && a.expiresAt == b.expiresAt &&
        a.version == b.version && a.confidence == b.confidence && a.status == b.status;
}

bool isValidHazardType(HazardType type)
{
    return type >= HazardType::RoadObstacle && type <= HazardType::Other;
}

bool isValidCoordinates(double latitude, double longitude)
{
    return std::isfinite(latitude) && latitude >= -90 && latitude <= 90 &&
           std::isfinite(longitude) && longitude >= -180 && longitude <= 180;
}

bool isValidHazardRecord(const HazardRecord &r)
{
    return r.originNodeId != 0 && (r.id >> 32) == r.originNodeId &&
           static_cast<uint32_t>(r.id) != 0 && r.updatedByNodeId != 0 &&
           isValidHazardType(r.type) && r.severity >= 1 && r.severity <= 5 &&
           isValidCoordinates(r.latitude, r.longitude) && r.createdAt != 0 &&
           r.lastObservedAt >= r.createdAt && r.locationFixAt != 0 &&
           r.locationFixAt <= r.lastObservedAt &&
           std::isfinite(r.locationAccuracyMeters) && r.locationAccuracyMeters > 0 &&
           r.expiresAt > r.lastObservedAt && r.version != 0 && r.confidence <= 100 &&
           r.status >= HazardStatus::Unverified && r.status <= HazardStatus::Stale;
}

double hazardDistanceMeters(double lat1, double lon1, double lat2, double lon2)
{
    if (!isValidCoordinates(lat1, lon1) || !isValidCoordinates(lat2, lon2)) return NAN;
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * radians;
    const double dlon = (lon2 - lon1) * radians;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * radians) * std::cos(lat2 * radians) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    if (a > 1) a = 1;
    if (a < 0) a = 0;
    return 6371000.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}
