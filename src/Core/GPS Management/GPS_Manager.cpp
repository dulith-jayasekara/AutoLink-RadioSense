/**
 * @file GPS_Manager.cpp
 * @brief Phone GPS data parsing, validation, storage, and retrieval.
 * @details Owns all GPS-specific state and diagnostics previously held by the
 * captive portal. Beacon construction remains in the Beacon module.
 */

#include "GPS_Manager.h"

namespace
{
    // Existing GPS state variables.
    String vehicleLatitude = "";
    String vehicleLongitude = "";
    String locationAccuracy = "";
    double latestLatitude = 0.0;
    double latestLongitude = 0.0;
    double latestSpeedMetersPerSecond = 0.0;
    double latestHeadingDegrees = 0.0;
    uint64_t latestFixTimestampMilliseconds = 0;
    bool hasLatestSpeed = false;
    bool hasLatestHeading = false;
    bool hasValidLocation = false;

    // Parse a finite floating-point value with the existing full-input rule.
    bool ParseFiniteDouble(const String &text, double &value)
    {
        const char *start = text.c_str();
        char *end = nullptr;
        const double parsedValue = strtod(start, &end);

        if (end == start || *end != '\0' || !isfinite(parsedValue))
        {
            return false;
        }

        value = parsedValue;
        return true;
    }

    // Preserve "null" as unavailable while parsing other values normally.
    bool ParseOptionalDouble(
        const String &text,
        double &value,
        bool &available)
    {
        if (text == "null")
        {
            value = 0.0;
            available = false;
            return true;
        }

        available = ParseFiniteDouble(text, value);
        return available;
    }
}

// Process, validate, store, and log one complete phone GPS fix.
GPS_ProcessResult GPS_ProcessLocation(
    const String &latitudeText,
    const String &longitudeText,
    const String &accuracyText,
    const String &speedText,
    const String &headingText,
    const String &fixTimestampText)
{
    vehicleLatitude = latitudeText;
    vehicleLongitude = longitudeText;
    locationAccuracy = accuracyText;

    double latitude = 0.0;
    double longitude = 0.0;
    double accuracy = 0.0;
    double speedMetersPerSecond = 0.0;
    double headingDegrees = 0.0;
    bool speedAvailable = false;
    bool headingAvailable = false;
    uint64_t fixTimestampMilliseconds = 0;

    if (!ParseFiniteDouble(vehicleLatitude, latitude) ||
        !ParseFiniteDouble(vehicleLongitude, longitude) ||
        !ParseFiniteDouble(locationAccuracy, accuracy) ||
        !ParseOptionalDouble(speedText, speedMetersPerSecond, speedAvailable) ||
        !ParseOptionalDouble(headingText, headingDegrees, headingAvailable) ||
        !Time_ParsePhoneTimestamp(
            fixTimestampText,
            fixTimestampMilliseconds) ||
        !Time_IsTimestampReasonable(fixTimestampMilliseconds))
    {
        return GPS_PROCESS_INVALID;
    }

    if (latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0 ||
        accuracy <= 0.0 ||
        (speedAvailable &&
            (speedMetersPerSecond < 0.0 || speedMetersPerSecond > 655.35)) ||
        (headingAvailable &&
            (headingDegrees < 0.0 || headingDegrees >= 360.0)))
    {
        return GPS_PROCESS_OUT_OF_RANGE;
    }

    // A previously accepted fix rejects an older timestamp.
    if (hasValidLocation &&
        fixTimestampMilliseconds < latestFixTimestampMilliseconds)
    {
        return GPS_PROCESS_STALE;
    }

    latestLatitude = latitude;
    latestLongitude = longitude;
    latestSpeedMetersPerSecond = speedMetersPerSecond;
    latestHeadingDegrees = headingDegrees;
    latestFixTimestampMilliseconds = fixTimestampMilliseconds;
    hasLatestSpeed = speedAvailable;
    hasLatestHeading = headingAvailable;
    hasValidLocation = true;

    Serial.println();
    Serial.println("===== GPS LOCATION RECEIVED =====");
    Serial.print("Latitude: ");
    Serial.println(vehicleLatitude);
    Serial.print("Longitude: ");
    Serial.println(vehicleLongitude);
    Serial.print("Accuracy (metres): ");
    Serial.println(accuracy, 1);
    if (accuracy > 1.0)
    {
        Serial.println("Accuracy warning: fix is above the 1 m target; using best available.");
    }
    Serial.print("Speed: ");
    if (hasLatestSpeed)
    {
        Serial.print(latestSpeedMetersPerSecond * 3.6, 2);
        Serial.println(" km/h");
    }
    else
    {
        Serial.println("unavailable from phone GPS");
    }
    Serial.print("Heading: ");
    if (hasLatestHeading)
    {
        Serial.print(latestHeadingDegrees, 1);
        Serial.println(" degrees");
    }
    else
    {
        Serial.println("unavailable from phone GPS");
    }
    Serial.print("GPS fix timestamp (Unix ms): ");
    Serial.printf(
        "%llu\n",
        static_cast<unsigned long long>(latestFixTimestampMilliseconds)
    );
    Serial.println("=================================");

    return GPS_PROCESS_ACCEPTED;
}

// Retrieve the latest accepted latitude and longitude.
bool GPS_GetLocation(double &latitude, double &longitude)
{
    if (!hasValidLocation)
    {
        return false;
    }

    latitude = latestLatitude;
    longitude = latestLongitude;
    return true;
}

// Retrieve motion together with availability semantics.
bool GPS_GetVehicleMotion(GPSMotion &motion)
{
    motion.speedMetersPerSecond = latestSpeedMetersPerSecond;
    motion.headingDegrees = latestHeadingDegrees;
    motion.speedAvailable = hasValidLocation && hasLatestSpeed;
    motion.headingAvailable = hasValidLocation && hasLatestHeading;
    return hasValidLocation;
}
