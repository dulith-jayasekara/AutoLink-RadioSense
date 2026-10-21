#include "Hazard_Record.h"
#include "Library_Headers.h"


bool isValidHazardRecord(const HazardRecord &record)
{
    const bool validType = record.type == HazardType::RoadObstacle ||
                           record.type == HazardType::RoadDamage ||
                           record.type == HazardType::Flooding ||
                           record.type == HazardType::Accident ||
                           record.type == HazardType::Other;

    const bool validStatus = record.status == HazardStatus::Unverified ||
                             record.status == HazardStatus::Active ||
                             record.status == HazardStatus::Resolved ||
                             record.status == HazardStatus::Expired;

    return record.id != 0 &&
           validType &&
           std::isfinite(record.latitude) &&
           record.latitude >= -90.0 && record.latitude <= 90.0 &&
           std::isfinite(record.longitude) &&
           record.longitude >= -180.0 && record.longitude <= 180.0 &&
           record.confidence <= 100 &&
           validStatus;
}



