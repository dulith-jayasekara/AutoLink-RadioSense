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
    // Store time as whole seconds plus a millisecond remainder. The latter
    // preserves sub-second precision between calls to the timestamp getters.
    uint64_t currentTimestampSeconds = 0;
    uint16_t remainingTimestampMilliseconds = 0;
    // millis() value from the last synchronization or accumulator update.
    uint32_t lastTimestampUpdateMillis = 0;
}

// Parse an unsigned decimal timestamp without signs, whitespace, or overflow.
bool Time_ParsePhoneTimestamp(const String &text, uint64_t &timestampMilliseconds)
{
    // Parse digit by digit instead of using a permissive conversion routine:
    // signs, spaces, and trailing characters are not valid Unix timestamps.
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
        // Check before multiplying so an unusually long input cannot wrap.
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
    // Reject values outside the project's supported Unix-date window.
    return timestampMilliseconds >= MIN_PHONE_TIMESTAMP_MS && timestampMilliseconds <= MAX_PHONE_TIMESTAMP_MS;
}

bool Time_Synchronization(uint64_t timestampMilliseconds)
{
    // Validate here as well as at the HTTP request boundary so every caller
    // must satisfy the same supported-date rule before changing the clock.
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

    // Update the ESP32 system clock. Do not mark the clock synchronized if
    // the operating-system clock update fails.
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
    // Zero indicates that time is unavailable or has not been synchronized.
    if (!clockSynchronized)
    {
        return 0;
    }

    const uint32_t nowMillis = millis();
    // Unsigned subtraction remains correct when millis() wraps around.
    const uint32_t elapsedMilliseconds = nowMillis - lastTimestampUpdateMillis;
    // Carry fractional seconds forward so repeated reads do not discard the
    // sub-second portion of elapsed time.
    const uint64_t accumulatedMilliseconds = static_cast<uint64_t>(remainingTimestampMilliseconds) + elapsedMilliseconds;
    currentTimestampSeconds += accumulatedMilliseconds / 1000ULL;
    remainingTimestampMilliseconds = static_cast<uint16_t>(accumulatedMilliseconds % 1000ULL);
    lastTimestampUpdateMillis = nowMillis;

    if (currentTimestampSeconds < MIN_UNIX_TIMESTAMP_SECONDS || currentTimestampSeconds > UINT32_MAX)
    {
        return 0;
    }

    return static_cast<uint32_t>(currentTimestampSeconds);
}

bool Time_GetCurrentTimestampMilliseconds(uint64_t &timestampMilliseconds)
{
    // GPS fixes are compared in milliseconds, so this accessor has a bool
    // result to distinguish unavailable time from a valid timestamp value.
    if (!clockSynchronized)
    {
        return false;
    }

    // Use the phone-synchronized accumulator rather than gettimeofday() so
    // GPS age checks and beacon timestamps share the same monotonic time base.
    const uint32_t nowMillis = millis();
    const uint32_t elapsedMilliseconds = nowMillis - lastTimestampUpdateMillis;
    const uint64_t accumulatedMilliseconds = static_cast<uint64_t>(remainingTimestampMilliseconds) + elapsedMilliseconds;
    
    currentTimestampSeconds += accumulatedMilliseconds / 1000ULL;
    remainingTimestampMilliseconds = static_cast<uint16_t>(accumulatedMilliseconds % 1000ULL);
    lastTimestampUpdateMillis = nowMillis;

    if (currentTimestampSeconds < MIN_UNIX_TIMESTAMP_SECONDS || currentTimestampSeconds > UINT32_MAX)
    {
        return false;
    }

    timestampMilliseconds = currentTimestampSeconds * 1000ULL + remainingTimestampMilliseconds;
    return true;
}

bool Time_IsSynchronized()
{
    // This state is reset at startup and becomes true only after a successful
    // phone-time synchronization.
    return clockSynchronized;
}
