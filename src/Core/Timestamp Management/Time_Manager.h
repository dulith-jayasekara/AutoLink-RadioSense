/**
 * @file Time_Manager.h
 * @brief Public interface for timestamp parsing and system-clock management.
 * @details Relocated as part of the Core timestamp module refactoring.
 */

#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

// Parse an unsigned decimal Unix timestamp in milliseconds.
bool Time_ParsePhoneTimestamp(
    const String &text,
    uint64_t &timestampMilliseconds
);

// Clear the synchronized state at startup; ESP32 system time is not assumed
// to survive a restart or loss of power.
void Time_Initialization();

// Check whether a phone timestamp in milliseconds is within the supported range.
bool Time_IsTimestampReasonable(uint64_t timestampMilliseconds);

// Set the ESP32 system clock from Unix milliseconds received from the phone.
bool Time_Synchronization(uint64_t timestampMilliseconds);

// Advance the stored Unix timestamp by elapsed ESP32 milliseconds and return
// whole seconds, or zero while unsynchronized / outside the packet's range.
uint32_t Time_GetCurrentTimestamp();

// Read the synchronized system clock in Unix milliseconds.
bool Time_GetCurrentTimestampMilliseconds(uint64_t &timestampMilliseconds);

// Report whether this boot has received and accepted a phone timestamp.
bool Time_IsSynchronized();

#endif
