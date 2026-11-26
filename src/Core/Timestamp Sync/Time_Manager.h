#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <stdint.h>

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

// Report whether this boot has received and accepted a phone timestamp.
bool Time_IsSynchronized();

#endif
