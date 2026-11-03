#pragma once

#include <stdint.h>

enum class HazardType : uint8_t
{
    Unknown = 0,
    RoadObstacle,
    RoadDamage,
    Flooding,
    Accident,
    Other
};

enum class HazardStatus : uint8_t
{
    Unverified = 0,
    Active,
    Resolved,
    Expired
};

struct HazardRecord
{
    uint32_t id;
    HazardType type;
    double latitude;
    double longitude;
    uint32_t createdAt;
    uint32_t lastObservedAt;
    uint16_t version;
    uint8_t confidence;
    HazardStatus status;
};

bool isValidHazardRecord(const HazardRecord &record);
