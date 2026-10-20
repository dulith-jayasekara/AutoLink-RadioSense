#pragma once
#include "Hazard_Service.h"
#include <stddef.h>

enum class ConsoleAction { Help, Time, Report, Revise, Resolve, List, Get, Type, Near, Memory, Sync, Groups, Evidence,
                           Confirm, Contradict, Confidence };
struct HazardCommand
{
    ConsoleAction action;
    PhoneHazardReport report;
    HazardId id;
    uint64_t clientReportId;
    uint32_t utc;
    HazardType type;
    double latitude;
    double longitude;
    double radiusMeters;
};
// Parses in place. Exact argument counts and numeric ranges are enforced.
bool parseHazardCommand(char *line, HazardCommand &command);

class PhoneClock
{
public:
    PhoneClock() : utc(0), lastTick(0), remainder(0) {}
    bool set(uint32_t value, uint32_t tick);
    uint32_t now(uint32_t tick);
private:
    uint32_t utc;
    uint32_t lastTick;
    uint32_t remainder;
};
