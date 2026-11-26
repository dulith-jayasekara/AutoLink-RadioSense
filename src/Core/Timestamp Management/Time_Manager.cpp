/**
 * @file Time_Manager.cpp
 * @brief Phone timestamp parsing and ESP32 system-clock synchronization.
 * @details Owns timestamp parsing, accepted-range validation, and clock maintenance.
 * Relationships: Used by Portal_Request_Handlers.cpp and GPS_Manager.cpp.
 */

#include "Time_Manager.h"

namespace
{
    // Prototype range: accept dates from 2020-01-01 through 2100-01-01 UTC.
    // Adjust these bounds if the project needs to support a wider date range.
    constexpr uint64_t MIN_PHONE_TIMESTAMP_MS = 1577836800000ULL;
    constexpr uint64_t MAX_PHONE_TIMESTAMP_MS = 4102444800000ULL;
    constexpr uint32_t MIN_UNIX_TIMESTAMP_SECONDS = 1577836800UL;

    // This flag is deliberately reset on every boot, even if the hardware
    // happens to retain some old system-clock value.
    bool clockSynchronized = false;
    uint64_t currentTimestampSeconds = 0;
    uint16_t remainingTimestampMilliseconds = 0;
    uint32_t lastTimestampUpdateMillis = 0;
}

// Parse an unsigned decimal timestamp without signs, whitespace, or overflow.
bool Time_ParsePhoneTimestamp(
    const String &text,
    uint64_t &timestampMilliseconds)
{
    if (text.length() == 0)
    {
        return false;
    }

    const uint64_t maximumValue = ~static_cast<uint64_t>(0);
    uint64_t parsedValue = 0;

    for (size_t index = 0; index < text.length(); ++index)
    {
        const char character = text.charAt(index);
        if (character < '0' || character > '9')
        {
            return false;
        }

        const uint64_t digit = static_cast<uint64_t>(character - '0');
        if (parsedValue > (maximumValue - digit) / 10ULL)
        {
            return false;
        }

        parsedValue = parsedValue * 10ULL + digit;
    }

    timestampMilliseconds = parsedValue;
    return true;
}

void Time_Initialization()
{
    clockSynchronized = false;
    currentTimestampSeconds = 0;
    remainingTimestampMilliseconds = 0;
    lastTimestampUpdateMillis = millis();
    Serial.println("System clock waiting for phone time synchronization.");
}

bool Time_IsTimestampReasonable(uint64_t timestampMilliseconds)
{
    return timestampMilliseconds >= MIN_PHONE_TIMESTAMP_MS &&
           timestampMilliseconds <= MAX_PHONE_TIMESTAMP_MS;
}

bool Time_Synchronization(uint64_t timestampMilliseconds)
{
    if (!Time_IsTimestampReasonable(timestampMilliseconds))
    {
        Serial.println("Rejected phone timestamp outside the supported range.");
        return false;
    }

    // settimeofday() expects whole seconds plus a microsecond remainder.
    // Date.now() is milliseconds, so convert its remainder to microseconds.
    const uint64_t seconds = timestampMilliseconds / 1000ULL;
    const uint64_t remainingMilliseconds = timestampMilliseconds % 1000ULL;

    timeval newTime = {};
    newTime.tv_sec = static_cast<time_t>(seconds);
    newTime.tv_usec = static_cast<suseconds_t>(remainingMilliseconds * 1000ULL);

    if (settimeofday(&newTime, nullptr) != 0)
    {
        Serial.println("Failed to set the ESP32 system clock.");
        return false;
    }

    // Keep the accepted phone time as our beacon timestamp base. Subsequent
    // reads advance this value using the ESP32 monotonic millisecond counter.
    currentTimestampSeconds = seconds;
    remainingTimestampMilliseconds = static_cast<uint16_t>(remainingMilliseconds);
    lastTimestampUpdateMillis = millis();
    clockSynchronized = true;
    Serial.print("System clock synchronized to Unix seconds: ");
    Serial.println(static_cast<uint32_t>(seconds));
    return true;
}

uint32_t Time_GetCurrentTimestamp()
{
    if (!clockSynchronized)
    {
        return 0;
    }

    const uint32_t nowMillis = millis();
    // Unsigned subtraction remains correct when millis() wraps around.
    const uint32_t elapsedMilliseconds = nowMillis - lastTimestampUpdateMillis;
    const uint64_t accumulatedMilliseconds =
        static_cast<uint64_t>(remainingTimestampMilliseconds) + elapsedMilliseconds;
    currentTimestampSeconds += accumulatedMilliseconds / 1000ULL;
    remainingTimestampMilliseconds =
        static_cast<uint16_t>(accumulatedMilliseconds % 1000ULL);
    lastTimestampUpdateMillis = nowMillis;

    if (currentTimestampSeconds < MIN_UNIX_TIMESTAMP_SECONDS ||
        currentTimestampSeconds > UINT32_MAX)
    {
        return 0;
    }

    return static_cast<uint32_t>(currentTimestampSeconds);
}

bool Time_IsSynchronized()
{
    return clockSynchronized;
}
