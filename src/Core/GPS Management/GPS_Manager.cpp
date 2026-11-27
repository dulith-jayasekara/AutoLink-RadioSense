/**
 * @file GPS_Manager.cpp
 * @brief Coherent phone GPS snapshot storage and freshness lifecycle.
 * @details Accepts validated measurements, tracks their original age, expires
 * fields independently, and exposes only operational values to other modules.
 */

#include "GPS_Manager.h"

namespace
{
    GPSMeasurementSnapshot latestSnapshot = {};
    bool hasSnapshot = false;
    uint64_t lastAcceptedFixTimestampMilliseconds = 0;
    uint64_t initialMeasurementAgeMilliseconds = 0;

    // Print only state changes, not every main-loop evaluation.
    const char *StateName(GPS_FreshnessState state)
    {
        switch (state)
        {
        case GPS_FRESH:
            return "FRESH";
        case GPS_STALE:
            return "STALE";
        case GPS_EXPIRED:
            return "EXPIRED";
        case GPS_UNAVAILABLE:
        default:
            return "UNAVAILABLE";
        }
    }

    void LogStateTransition(
        const char *field,
        GPS_FreshnessState previous,
        GPS_FreshnessState current)
    {
        if (previous != current)
        {
            Serial.print("GPS ");
            Serial.print(field);
            Serial.print(" state: ");
            Serial.print(StateName(previous));
            Serial.print(" -> ");
            Serial.println(StateName(current));
        }
    }

    bool IsOperational(GPS_FreshnessState state)
    {
        return state == GPS_FRESH || state == GPS_STALE;
    }

    uint32_t GetSpeedExpirationThresholdMilliseconds(
        bool speedAvailable,
        double speedMetersPerSecond)
    {
        return speedAvailable && speedMetersPerSecond == GPS_MIN_SPEED_MPS
            ? GPS_ZERO_SPEED_EXPIRE_MS
            : GPS_SPEED_EXPIRE_MS;
    }

    uint64_t GetEffectiveMeasurementAgeMilliseconds()
    {
        const uint32_t elapsedSinceReception =
            millis() - latestSnapshot.receivedAtMilliseconds;
        uint64_t age =
            initialMeasurementAgeMilliseconds + elapsedSinceReception;

        // A synchronized wall clock may only shorten the remaining lifetime;
        // clock changes can never make an already older estimate younger.
        uint64_t currentUnixMilliseconds = 0;
        if (Time_GetCurrentTimestampMilliseconds(currentUnixMilliseconds) &&
            currentUnixMilliseconds >= latestSnapshot.fixTimestampMilliseconds)
        {
            const uint64_t wallClockAge =
                currentUnixMilliseconds - latestSnapshot.fixTimestampMilliseconds;
            if (wallClockAge > age)
            {
                age = wallClockAge;
            }
        }

        return age;
    }

    GPS_FreshnessState AdvanceState(
        GPS_FreshnessState currentState,
        bool initiallyAvailable,
        uint64_t ageMilliseconds,
        uint32_t freshnessThresholdMilliseconds,
        uint32_t expirationThresholdMilliseconds)
    {
        if (currentState == GPS_EXPIRED)
        {
            return GPS_EXPIRED;
        }

        return GPS_DetermineFreshnessState(
            initiallyAvailable,
            ageMilliseconds,
            freshnessThresholdMilliseconds,
            expirationThresholdMilliseconds
        );
    }
}

// Parse, validate, age-check, and atomically replace a complete phone snapshot.
GPS_ProcessResult GPS_ProcessLocation(
    const String &latitudeText,
    const String &longitudeText,
    const String &accuracyText,
    const String &speedText,
    const String &headingText,
    const String &fixTimestampText)
{
    double latitude = 0.0;
    double longitude = 0.0;
    double accuracy = 0.0;
    double speedMetersPerSecond = 0.0;
    double headingDegrees = 0.0;

    const GPS_ParseResult latitudeResult =
        GPS_ParseFiniteDouble(latitudeText, latitude);
    const GPS_ParseResult longitudeResult =
        GPS_ParseFiniteDouble(longitudeText, longitude);
    const GPS_ParseResult accuracyResult =
        GPS_ParseFiniteDouble(accuracyText, accuracy);

    if (latitudeResult != GPS_PARSE_VALID ||
        longitudeResult != GPS_PARSE_VALID ||
        accuracyResult != GPS_PARSE_VALID)
    {
        Serial.println("GPS measurement rejected: required position or accuracy input is unavailable or malformed.");
        return GPS_PROCESS_INVALID;
    }

    if (!GPS_ValidatePosition(latitude, longitude) ||
        !GPS_ValidateAccuracy(accuracy))
    {
        Serial.println("GPS measurement rejected: position or accuracy is outside its configured range.");
        return GPS_PROCESS_OUT_OF_RANGE;
    }

    bool speedAvailable =
        GPS_ParseOptionalDouble(speedText, speedMetersPerSecond) ==
        GPS_PARSE_VALID;
    bool speedWasClamped = false;
    const double receivedSpeedMetersPerSecond = speedMetersPerSecond;
    if (speedAvailable &&
        !GPS_ValidateSpeed(speedMetersPerSecond, speedWasClamped))
    {
        speedAvailable = false;
    }

    bool headingAvailable =
        GPS_ParseOptionalDouble(headingText, headingDegrees) ==
        GPS_PARSE_VALID;
    if (headingAvailable && !GPS_ValidateHeading(headingDegrees))
    {
        headingAvailable = false;
    }

    uint64_t fixTimestampMilliseconds = 0;
    if (!Time_ParsePhoneTimestamp(
            fixTimestampText,
            fixTimestampMilliseconds) ||
        !GPS_ValidateFixTimestamp(fixTimestampMilliseconds))
    {
        Serial.println("GPS measurement rejected: fix timestamp is invalid or outside the supported range.");
        return GPS_PROCESS_INVALID;
    }

    if (hasSnapshot &&
        fixTimestampMilliseconds <= lastAcceptedFixTimestampMilliseconds)
    {
        if (fixTimestampMilliseconds == lastAcceptedFixTimestampMilliseconds)
        {
            Serial.println("GPS duplicate measurement ignored; freshness timer unchanged.");
        }
        else
        {
            Serial.println("GPS out-of-order measurement ignored; freshness timer unchanged.");
        }
        return GPS_PROCESS_STALE;
    }

    uint64_t measurementAgeAtReceptionMilliseconds = 0;
    uint64_t currentUnixMilliseconds = 0;
    if (Time_GetCurrentTimestampMilliseconds(currentUnixMilliseconds))
    {
        if (fixTimestampMilliseconds > currentUnixMilliseconds)
        {
            const uint64_t futureOffset =
                fixTimestampMilliseconds - currentUnixMilliseconds;
            if (futureOffset > GPS_FUTURE_TIMESTAMP_TOLERANCE_MS)
            {
                Serial.println("GPS measurement rejected: fix timestamp is excessively far in the future.");
                return GPS_PROCESS_INVALID;
            }
        }
        else
        {
            measurementAgeAtReceptionMilliseconds =
                currentUnixMilliseconds - fixTimestampMilliseconds +
                GPS_TIMESTAMP_AGE_UNCERTAINTY_MS;
        }
        if (fixTimestampMilliseconds > currentUnixMilliseconds)
        {
            measurementAgeAtReceptionMilliseconds +=
                fixTimestampMilliseconds - currentUnixMilliseconds +
                GPS_TIMESTAMP_AGE_UNCERTAINTY_MS;
        }
    }
    else
    {
        Serial.println("GPS clock unavailable; using monotonic reception age (cached fix age cannot be verified).");
    }

    const double thresholdSpeedMetersPerSecond = speedAvailable
        ? speedMetersPerSecond
        : GPS_POSITION_FALLBACK_SPEED_MPS;
    const uint32_t positionFreshnessThresholdMilliseconds =
        GPS_CalculatePositionFreshnessThresholdMs(
            thresholdSpeedMetersPerSecond);
    const uint32_t positionExpirationThresholdMilliseconds =
        GPS_CalculatePositionExpirationThresholdMs(
            thresholdSpeedMetersPerSecond);

    if (positionFreshnessThresholdMilliseconds >=
        positionExpirationThresholdMilliseconds)
    {
        Serial.println("GPS snapshot rejected: configured position thresholds are inconsistent.");
        return GPS_PROCESS_INVALID;
    }

    const GPS_FreshnessState initialPositionState =
        GPS_DetermineFreshnessState(
            true,
            measurementAgeAtReceptionMilliseconds,
            positionFreshnessThresholdMilliseconds,
            positionExpirationThresholdMilliseconds);
    if (initialPositionState == GPS_EXPIRED)
    {
        Serial.println("GPS measurement rejected: position was already expired when received.");
        return GPS_PROCESS_STALE;
    }

    GPSMeasurementSnapshot acceptedSnapshot = {};
    acceptedSnapshot.latitude = latitude;
    acceptedSnapshot.longitude = longitude;
    acceptedSnapshot.accuracyMeters = accuracy;
    acceptedSnapshot.speedMetersPerSecond = speedMetersPerSecond;
    acceptedSnapshot.headingDegrees = headingDegrees;
    acceptedSnapshot.fixTimestampMilliseconds =
        fixTimestampMilliseconds;
    acceptedSnapshot.effectiveAgeMilliseconds =
        measurementAgeAtReceptionMilliseconds;
    acceptedSnapshot.receivedAtMilliseconds = millis();
    acceptedSnapshot.positionFreshnessThresholdMilliseconds =
        positionFreshnessThresholdMilliseconds;
    acceptedSnapshot.positionExpirationThresholdMilliseconds =
        positionExpirationThresholdMilliseconds;
    acceptedSnapshot.positionState = initialPositionState;
    acceptedSnapshot.speedState = GPS_DetermineFreshnessState(
        speedAvailable,
        measurementAgeAtReceptionMilliseconds,
        GPS_SPEED_FRESH_MS,
        GetSpeedExpirationThresholdMilliseconds(
            speedAvailable,
            speedMetersPerSecond));
    acceptedSnapshot.headingState = GPS_DetermineFreshnessState(
        headingAvailable,
        measurementAgeAtReceptionMilliseconds,
        GPS_HEADING_FRESH_MS,
        GPS_HEADING_EXPIRE_MS);
    acceptedSnapshot.positionAvailable =
        IsOperational(acceptedSnapshot.positionState);
    acceptedSnapshot.speedAvailable =
        IsOperational(acceptedSnapshot.speedState);
    acceptedSnapshot.headingAvailable =
        IsOperational(acceptedSnapshot.headingState);
    acceptedSnapshot.timestampAvailable =
        acceptedSnapshot.positionAvailable ||
        acceptedSnapshot.speedAvailable ||
        acceptedSnapshot.headingAvailable;

    const GPS_FreshnessState previousPositionState =
        hasSnapshot ? latestSnapshot.positionState : GPS_UNAVAILABLE;
    const GPS_FreshnessState previousSpeedState =
        hasSnapshot ? latestSnapshot.speedState : GPS_UNAVAILABLE;
    const GPS_FreshnessState previousHeadingState =
        hasSnapshot ? latestSnapshot.headingState : GPS_UNAVAILABLE;

    latestSnapshot = acceptedSnapshot;
    initialMeasurementAgeMilliseconds =
        measurementAgeAtReceptionMilliseconds;
    lastAcceptedFixTimestampMilliseconds =
        fixTimestampMilliseconds;
    hasSnapshot = true;

    LogStateTransition(
        "position",
        previousPositionState,
        latestSnapshot.positionState);
    LogStateTransition(
        "speed",
        previousSpeedState,
        latestSnapshot.speedState);
    LogStateTransition(
        "heading",
        previousHeadingState,
        latestSnapshot.headingState);

    if (speedWasClamped)
    {
        Serial.print("GPS speed clamped from ");
        Serial.print(receivedSpeedMetersPerSecond * 3.6, 2);
        Serial.print(" km/h to ");
        Serial.print(speedMetersPerSecond * 3.6, 2);
        Serial.println(" km/h.");
    }
    Serial.print("GPS fix accepted: lat=");
    Serial.print(latitudeText);
    Serial.print(" lon=");
    Serial.print(longitudeText);
    Serial.print(" accuracy=");
    Serial.print(accuracy, 1);
    Serial.print("m");
    if (accuracy > 1.0)
    {
        Serial.print(" (above 1 m target)");
    }
    Serial.print(" speed=");
    if (speedAvailable)
    {
        Serial.print(speedMetersPerSecond * 3.6, 2);
        Serial.print("km/h");
    }
    else
    {
        Serial.print("unavailable (100 km/h assumed for position thresholds)");
    }
    Serial.print(" heading=");
    if (headingAvailable)
    {
        Serial.print(headingDegrees, 1);
        Serial.print("deg");
    }
    else
    {
        Serial.print("unavailable");
    }
    Serial.print(" age=");
    Serial.printf(
        "%llu",
        static_cast<unsigned long long>(
            measurementAgeAtReceptionMilliseconds));
    Serial.print("ms position thresholds=");
    Serial.print(positionFreshnessThresholdMilliseconds);
    Serial.print("/");
    Serial.print(positionExpirationThresholdMilliseconds);
    Serial.println("ms");

    return GPS_PROCESS_ACCEPTED;
}

// Update all independent states; expired states remain latched until recovery.
void GPS_UpdateFreshness()
{
    if (!hasSnapshot)
    {
        return;
    }

    const uint64_t ageMilliseconds =
        GetEffectiveMeasurementAgeMilliseconds();
    const GPS_FreshnessState previousPositionState =
        latestSnapshot.positionState;
    const GPS_FreshnessState previousSpeedState =
        latestSnapshot.speedState;
    const GPS_FreshnessState previousHeadingState =
        latestSnapshot.headingState;

    latestSnapshot.effectiveAgeMilliseconds = ageMilliseconds;
    latestSnapshot.positionState = AdvanceState(
        latestSnapshot.positionState,
        true,
        ageMilliseconds,
        latestSnapshot.positionFreshnessThresholdMilliseconds,
        latestSnapshot.positionExpirationThresholdMilliseconds);
    latestSnapshot.speedState = AdvanceState(
        latestSnapshot.speedState,
        latestSnapshot.speedState != GPS_UNAVAILABLE,
        ageMilliseconds,
        GPS_SPEED_FRESH_MS,
        GetSpeedExpirationThresholdMilliseconds(
            latestSnapshot.speedState != GPS_UNAVAILABLE,
            latestSnapshot.speedMetersPerSecond));
    latestSnapshot.headingState = AdvanceState(
        latestSnapshot.headingState,
        latestSnapshot.headingState != GPS_UNAVAILABLE,
        ageMilliseconds,
        GPS_HEADING_FRESH_MS,
        GPS_HEADING_EXPIRE_MS);

    latestSnapshot.positionAvailable =
        IsOperational(latestSnapshot.positionState);
    latestSnapshot.speedAvailable =
        IsOperational(latestSnapshot.speedState);
    latestSnapshot.headingAvailable =
        IsOperational(latestSnapshot.headingState);
    latestSnapshot.timestampAvailable =
        latestSnapshot.positionAvailable ||
        latestSnapshot.speedAvailable ||
        latestSnapshot.headingAvailable;

    LogStateTransition(
        "position",
        previousPositionState,
        latestSnapshot.positionState);
    LogStateTransition(
        "speed",
        previousSpeedState,
        latestSnapshot.speedState);
    LogStateTransition(
        "heading",
        previousHeadingState,
        latestSnapshot.headingState);
}

// Retrieve the latest accepted snapshot, refreshing expiry states first.
bool GPS_GetSnapshot(GPSMeasurementSnapshot &snapshot)
{
    GPS_UpdateFreshness();
    if (!hasSnapshot)
    {
        snapshot = {};
        snapshot.positionState = GPS_UNAVAILABLE;
        snapshot.speedState = GPS_UNAVAILABLE;
        snapshot.headingState = GPS_UNAVAILABLE;
        return false;
    }

    snapshot = latestSnapshot;
    if (!snapshot.positionAvailable)
    {
        snapshot.latitude = NAN;
        snapshot.longitude = NAN;
        snapshot.accuracyMeters = NAN;
    }
    if (!snapshot.speedAvailable)
    {
        snapshot.speedMetersPerSecond = NAN;
    }
    if (!snapshot.headingAvailable)
    {
        snapshot.headingDegrees = NAN;
    }
    return true;
}

// Retrieve a position only while its shared latitude/longitude state is live.
bool GPS_GetLocation(double &latitude, double &longitude)
{
    GPSMeasurementSnapshot snapshot = {};
    if (!GPS_GetSnapshot(snapshot) || !snapshot.positionAvailable)
    {
        latitude = NAN;
        longitude = NAN;
        return false;
    }

    latitude = snapshot.latitude;
    longitude = snapshot.longitude;
    return true;
}

// Motion availability is independent of the geographic position state.
bool GPS_GetVehicleMotion(GPSMotion &motion)
{
    GPSMeasurementSnapshot snapshot = {};
    if (!GPS_GetSnapshot(snapshot))
    {
        motion.speedMetersPerSecond = NAN;
        motion.headingDegrees = NAN;
        motion.speedAvailable = false;
        motion.headingAvailable = false;
        return false;
    }

    motion.speedMetersPerSecond = snapshot.speedMetersPerSecond;
    motion.headingDegrees = snapshot.headingDegrees;
    motion.speedAvailable = snapshot.speedAvailable;
    motion.headingAvailable = snapshot.headingAvailable;
    return motion.speedAvailable || motion.headingAvailable;
}
