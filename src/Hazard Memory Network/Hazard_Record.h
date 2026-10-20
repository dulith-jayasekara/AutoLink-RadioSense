#pragma once
#include <stdint.h>

using HazardId = uint64_t;
enum class HazardType : uint8_t { Unknown = 0, RoadObstacle, RoadDamage, Flooding, Accident, Other };
// Keep existing numeric values; append Stale.
enum class HazardStatus : uint8_t { Unverified = 0, Active, Resolved, Expired, Stale };

struct HazardRecord
{
    HazardId id; // Origin node in upper 32 bits, persistent sequence in lower 32.
    uint32_t originNodeId;
    uint32_t updatedByNodeId;
    HazardType type;
    uint8_t severity; // Prototype scale: 1 (lowest) through 5 (highest).
    double latitude;
    double longitude;
    uint32_t createdAt; // All timestamps are UTC Unix seconds.
    uint32_t lastObservedAt;
    uint32_t locationFixAt;
    float locationAccuracyMeters;
    uint32_t expiresAt;
    uint32_t version;
    uint8_t confidence;
    HazardStatus status;
};

bool isValidHazardType(HazardType type);
bool isValidCoordinates(double latitude, double longitude);
bool isValidHazardRecord(const HazardRecord &record);
bool sameHazardRecord(const HazardRecord &a, const HazardRecord &b);
double hazardDistanceMeters(double lat1, double lon1, double lat2, double lon2);
