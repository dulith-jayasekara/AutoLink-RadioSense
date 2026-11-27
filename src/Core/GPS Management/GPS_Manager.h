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

enum GPS_FreshnessState
{
    GPS_FRESH,
    GPS_STALE,
    GPS_EXPIRED,
    GPS_UNAVAILABLE
};

enum GPS_ParseResult
{
    GPS_PARSE_VALID,
    GPS_PARSE_UNAVAILABLE,
    GPS_PARSE_INVALID
};

struct GPSMotion
{
    double speedMetersPerSecond;
    double headingDegrees;
    bool speedAvailable;
    bool headingAvailable;
};

// Coherent view of the current original phone-measurement snapshot.
struct GPSMeasurementSnapshot
{
    double latitude;
    double longitude;
    double accuracyMeters;
    double speedMetersPerSecond;
    double headingDegrees;
    uint64_t fixTimestampMilliseconds;
    uint64_t effectiveAgeMilliseconds;
    uint32_t receivedAtMilliseconds;
    uint32_t positionFreshnessThresholdMilliseconds;
    uint32_t positionExpirationThresholdMilliseconds;
    GPS_FreshnessState positionState;
    GPS_FreshnessState speedState;
    GPS_FreshnessState headingState;
    bool positionAvailable;
    bool speedAvailable;
    bool headingAvailable;
    bool timestampAvailable;
};

// Parsing and validation helpers implemented in GPS_Validation.cpp.
GPS_ParseResult GPS_ParseFiniteDouble(const String &text, double &value);
GPS_ParseResult GPS_ParseOptionalDouble(const String &text, double &value);
bool GPS_ValidateLatitude(double latitude);
bool GPS_ValidateLongitude(double longitude);
bool GPS_ValidateAccuracy(double accuracyMeters);
bool GPS_ValidateSpeed(double &speedMetersPerSecond, bool &wasClamped);
bool GPS_ValidateHeading(double headingDegrees);
bool GPS_ValidateFixTimestamp(uint64_t timestampMilliseconds);
bool GPS_ValidatePosition(double latitude, double longitude);
uint32_t GPS_CalculatePositionFreshnessThresholdMs(double speedMetersPerSecond);
uint32_t GPS_CalculatePositionExpirationThresholdMs(double speedMetersPerSecond);

GPS_FreshnessState GPS_DetermineFreshnessState(
    bool measurementAvailable,
    uint64_t measurementAgeMilliseconds,
    uint32_t freshnessThresholdMilliseconds,
    uint32_t expirationThresholdMilliseconds);

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

// Refresh lifecycle states and retrieve all fields from one coherent snapshot.
void GPS_UpdateFreshness();
bool GPS_GetSnapshot(GPSMeasurementSnapshot &snapshot);

#endif
