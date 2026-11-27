/**
 * @file GPS_Validation.cpp
 * @brief Parsing, validation, and adaptive freshness calculations for GPS data.
 * @details Implements field-specific rules and the speed-adaptive position
 * thresholds shared by GPS_Manager.cpp.
 */

#include "GPS_Manager.h"

#include <errno.h>
#include <stdlib.h>

GPS_ParseResult GPS_ParseFiniteDouble(const String &text, double &value)
{
    if (text.length() == 0 || text == "null")
    {
        return GPS_PARSE_UNAVAILABLE;
    }

    const char *start = text.c_str();
    for (const char *character = start; *character != '\0'; ++character)
    {
        if (isspace(static_cast<unsigned char>(*character)))
        {
            return GPS_PARSE_INVALID;
        }
    }

    errno = 0;
    char *end = nullptr;
    const double parsedValue = strtod(start, &end);
    if (end == start || *end != '\0' || errno == ERANGE ||
        !isfinite(parsedValue))
    {
        return GPS_PARSE_INVALID;
    }

    value = parsedValue;
    return GPS_PARSE_VALID;
}

GPS_ParseResult GPS_ParseOptionalDouble(const String &text, double &value)
{
    return GPS_ParseFiniteDouble(text, value);
}

bool GPS_ValidateLatitude(double latitude)
{
    const double encoded = latitude * 10000000.0;
    return isfinite(latitude) &&
           latitude >= -90.0 && latitude <= 90.0 &&
           encoded >= static_cast<double>(INT32_MIN + 1) &&
           encoded <= static_cast<double>(INT32_MAX);
}

bool GPS_ValidateLongitude(double longitude)
{
    const double encoded = longitude * 10000000.0;
    return isfinite(longitude) &&
           longitude >= -180.0 && longitude <= 180.0 &&
           encoded >= static_cast<double>(INT32_MIN + 1) &&
           encoded <= static_cast<double>(INT32_MAX);
}

bool GPS_ValidateAccuracy(double accuracyMeters)
{
    return isfinite(accuracyMeters) &&
           accuracyMeters >= 0.0 &&
           accuracyMeters <= GPS_MAX_ACCEPTABLE_ACCURACY_M;
}

bool GPS_ValidateSpeed(double &speedMetersPerSecond, bool &wasClamped)
{
    wasClamped = false;
    if (!isfinite(speedMetersPerSecond) ||
        speedMetersPerSecond < GPS_MIN_SPEED_MPS)
    {
        return false;
    }

    if (speedMetersPerSecond > GPS_MAX_SPEED_MPS)
    {
        speedMetersPerSecond = GPS_MAX_SPEED_MPS;
        wasClamped = true;
    }

    return true;
}

bool GPS_ValidateHeading(double headingDegrees)
{
    return isfinite(headingDegrees) &&
           headingDegrees >= 0.0 && headingDegrees < 360.0;
}

bool GPS_ValidateFixTimestamp(uint64_t timestampMilliseconds)
{
    if (!Time_IsTimestampReasonable(timestampMilliseconds))
    {
        return false;
    }

    const uint64_t timestampSeconds = timestampMilliseconds / 1000ULL;
    return timestampSeconds < static_cast<uint64_t>(GPS_TIMESTAMP_UNAVAILABLE);
}

bool GPS_ValidatePosition(double latitude, double longitude)
{
    return GPS_ValidateLatitude(latitude) &&
           GPS_ValidateLongitude(longitude);
}

uint32_t GPS_CalculatePositionFreshnessThresholdMs(
    double speedMetersPerSecond)
{
    if (speedMetersPerSecond == 0.0)
    {
        return GPS_POSITION_MAX_FRESH_MS;
    }

    if (!isfinite(speedMetersPerSecond) || speedMetersPerSecond < 0.0)
    {
        speedMetersPerSecond = GPS_POSITION_FALLBACK_SPEED_MPS;
    }

    const double rawThreshold =
        (GPS_POSITION_FRESH_DISTANCE_M / speedMetersPerSecond) * 1000.0;
    if (!isfinite(rawThreshold) ||
        rawThreshold >= static_cast<double>(GPS_POSITION_MAX_FRESH_MS))
    {
        return GPS_POSITION_MAX_FRESH_MS;
    }
    if (rawThreshold <= static_cast<double>(GPS_POSITION_MIN_FRESH_MS))
    {
        return GPS_POSITION_MIN_FRESH_MS;
    }

    return static_cast<uint32_t>(rawThreshold + 0.5);
}

uint32_t GPS_CalculatePositionExpirationThresholdMs(
    double speedMetersPerSecond)
{
    if (speedMetersPerSecond == 0.0)
    {
        return GPS_POSITION_MAX_EXPIRE_MS;
    }

    if (!isfinite(speedMetersPerSecond) || speedMetersPerSecond < 0.0)
    {
        speedMetersPerSecond = GPS_POSITION_FALLBACK_SPEED_MPS;
    }

    const double rawThreshold =
        (GPS_POSITION_EXPIRE_DISTANCE_M / speedMetersPerSecond) * 1000.0;
    if (!isfinite(rawThreshold) ||
        rawThreshold >= static_cast<double>(GPS_POSITION_MAX_EXPIRE_MS))
    {
        return GPS_POSITION_MAX_EXPIRE_MS;
    }
    if (rawThreshold <= static_cast<double>(GPS_POSITION_MIN_EXPIRE_MS))
    {
        return GPS_POSITION_MIN_EXPIRE_MS;
    }

    return static_cast<uint32_t>(rawThreshold + 0.5);
}

GPS_FreshnessState GPS_DetermineFreshnessState(
    bool measurementAvailable,
    uint64_t measurementAgeMilliseconds,
    uint32_t freshnessThresholdMilliseconds,
    uint32_t expirationThresholdMilliseconds)
{
    if (!measurementAvailable)
    {
        return GPS_UNAVAILABLE;
    }
    if (measurementAgeMilliseconds >= expirationThresholdMilliseconds)
    {
        return GPS_EXPIRED;
    }
    if (measurementAgeMilliseconds >= freshnessThresholdMilliseconds)
    {
        return GPS_STALE;
    }
    return GPS_FRESH;
}
