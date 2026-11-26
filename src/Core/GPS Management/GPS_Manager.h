/**
 * @file GPS_Manager.h
 * @brief Public interface for phone-derived GPS data.
 * @details Parses, validates, stores, and retrieves GPS and motion values.
 * Relationships: Used by Portal_Request_Handlers.cpp and Beacon/main code.
 */

#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

// Result of processing one submitted GPS fix.
enum GPS_ProcessResult
{
    GPS_PROCESS_ACCEPTED,
    GPS_PROCESS_INVALID,
    GPS_PROCESS_OUT_OF_RANGE,
    GPS_PROCESS_STALE
};

struct GPSMotion
{
    double speedMetersPerSecond;
    double headingDegrees;
    bool speedAvailable;
    bool headingAvailable;
};

// Process raw phone location fields and apply the existing GPS validations.
GPS_ProcessResult GPS_ProcessLocation(
    const String &latitudeText,
    const String &longitudeText,
    const String &accuracyText,
    const String &speedText,
    const String &headingText,
    const String &fixTimestampText
);

// Retrieve the latest accepted latitude and longitude.
bool GPS_GetLocation(double &latitude, double &longitude);

// Retrieve the latest accepted phone motion values.
bool GPS_GetVehicleMotion(GPSMotion &motion);

#endif
