#include "Hazard_Console.h"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
bool unsignedNumber(const char *text, uint64_t maximum, uint64_t &out)
{
    if (!text || !*text) return false;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char *end = nullptr;
    const unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end || value > maximum) return false;
    out = value;
    return true;
}
bool realNumber(const char *text, double &out)
{
    errno = 0;
    char *end = nullptr;
    out = strtod(text, &end);
    return *text && end != text && !*end && !errno && std::isfinite(out);
}
bool reportFields(char **tokens, PhoneHazardReport &r)
{
    uint64_t type, severity, observed, fix;
    double accuracy;
    if (!unsignedNumber(tokens[0], 5, type) || type == 0 ||
        !unsignedNumber(tokens[1], 5, severity) || severity == 0 ||
        !realNumber(tokens[2], r.latitude) || !realNumber(tokens[3], r.longitude) ||
        !unsignedNumber(tokens[4], UINT32_MAX, observed) ||
        !unsignedNumber(tokens[5], UINT32_MAX, fix) ||
        !realNumber(tokens[6], accuracy) || accuracy <= 0 || accuracy > HazardService::MAX_ACCURACY_METERS ||
        !isValidCoordinates(r.latitude, r.longitude)) return false;
    r.type = static_cast<HazardType>(type);
    r.severity = static_cast<uint8_t>(severity);
    r.observedAt = static_cast<uint32_t>(observed);
    r.locationFixAt = static_cast<uint32_t>(fix);
    r.locationAccuracyMeters = static_cast<float>(accuracy);
    return true;
}
}

bool parseHazardCommand(char *line, HazardCommand &c)
{
    c = HazardCommand{};
    if (!line) return false;
    char *tokens[10];
    size_t count = 0;
    char *state = nullptr;
    for (char *t = strtok_r(line, " \t\r\n", &state); t; t = strtok_r(nullptr, " \t\r\n", &state))
    {
        if (count == 10) return false;
        tokens[count++] = t;
    }
    if (!count) return false;
    if (count == 1)
    {
        if (!strcmp(tokens[0], "HELP")) c.action = ConsoleAction::Help;
        else if (!strcmp(tokens[0], "LIST")) c.action = ConsoleAction::List;
        else if (!strcmp(tokens[0], "MEMORY")) c.action = ConsoleAction::Memory;
        else if (!strcmp(tokens[0], "SYNC")) c.action = ConsoleAction::Sync;
        else if (!strcmp(tokens[0], "CLEANUP")) c.action = ConsoleAction::Cleanup;
        else if (!strcmp(tokens[0], "RETENTION")) c.action = ConsoleAction::Retention;
        else if (!strcmp(tokens[0], "RETIRED")) c.action = ConsoleAction::Retired;
        else if (!strcmp(tokens[0], "GROUPS")) c.action = ConsoleAction::Groups;
        else return false;
        return true;
    }
    if (!strcmp(tokens[0], "POSITION") && count == 3 && realNumber(tokens[1], c.latitude) &&
        realNumber(tokens[2], c.longitude) && isValidCoordinates(c.latitude, c.longitude))
    { c.action = ConsoleAction::Position; return true; }
    uint64_t number;
    uint64_t timestamp;
    uint64_t clientId;
    if ((!strcmp(tokens[0], "CONFIRM_ID") || !strcmp(tokens[0], "CONTRADICT_ID")) && count == 10 &&
        unsignedNumber(tokens[1], UINT64_MAX, clientId) && clientId &&
        unsignedNumber(tokens[2], UINT64_MAX, number) && number && reportFields(tokens + 3, c.report))
    { c.action = !strcmp(tokens[0], "CONFIRM_ID") ? ConsoleAction::Confirm : ConsoleAction::Contradict;
      c.clientReportId = clientId; c.id = number; return true; }
    if (!strcmp(tokens[0], "CONFIDENCE") && count == 2 &&
        unsignedNumber(tokens[1], UINT64_MAX, number) && number)
    { c.action = ConsoleAction::Confidence; c.id = number; return true; }
    if (!strcmp(tokens[0], "REPORT_ID") && count == 9 &&
        unsignedNumber(tokens[1], UINT64_MAX, clientId) && clientId && reportFields(tokens + 2, c.report))
    { c.action = ConsoleAction::Report; c.clientReportId = clientId; return true; }
    if (!strcmp(tokens[0], "REVISE_ID") && count == 10 &&
        unsignedNumber(tokens[1], UINT64_MAX, clientId) && clientId &&
        unsignedNumber(tokens[2], UINT64_MAX, number) && number && reportFields(tokens + 3, c.report))
    { c.action = ConsoleAction::Revise; c.clientReportId = clientId; c.id = number; return true; }
    if (!strcmp(tokens[0], "RESOLVE_ID") && count == 4 &&
        unsignedNumber(tokens[1], UINT64_MAX, clientId) && clientId &&
        unsignedNumber(tokens[2], UINT64_MAX, number) && number &&
        unsignedNumber(tokens[3], UINT32_MAX, timestamp) && timestamp)
    { c.action = ConsoleAction::Resolve; c.clientReportId = clientId; c.id = number;
      c.utc = static_cast<uint32_t>(timestamp); return true; }
    if (!strcmp(tokens[0], "EVIDENCE") && count == 2 &&
        unsignedNumber(tokens[1], UINT64_MAX, number) && number)
    { c.action = ConsoleAction::Evidence; c.id = number; return true; }
    if (!strcmp(tokens[0], "RESOLVE") && count == 3 &&
        unsignedNumber(tokens[1], UINT64_MAX, number) && number != 0 &&
        unsignedNumber(tokens[2], UINT32_MAX, timestamp) && timestamp != 0)
    {
        c.action = ConsoleAction::Resolve;
        c.id = number;
        c.utc = static_cast<uint32_t>(timestamp);
        return true;
    }
    if (!strcmp(tokens[0], "TIME") && count == 2 &&
        unsignedNumber(tokens[1], UINT32_MAX, number) && number != 0)
    {
        c.action = ConsoleAction::Time;
        c.utc = static_cast<uint32_t>(number);
        return true;
    }
    if (!strcmp(tokens[0], "GET") && count == 2 &&
        unsignedNumber(tokens[1], UINT64_MAX, number) && number != 0)
    {
        c.action = ConsoleAction::Get;
        c.id = number;
        return true;
    }
    if (!strcmp(tokens[0], "TYPE") && count == 2 &&
        unsignedNumber(tokens[1], 5, number) && number != 0)
    {
        c.action = ConsoleAction::Type;
        c.type = static_cast<HazardType>(number);
        return true;
    }
    if (!strcmp(tokens[0], "NEAR") && count == 4 &&
        realNumber(tokens[1], c.latitude) && realNumber(tokens[2], c.longitude) &&
        realNumber(tokens[3], c.radiusMeters) && c.radiusMeters >= 0 &&
        isValidCoordinates(c.latitude, c.longitude))
    {
        c.action = ConsoleAction::Near;
        return true;
    }
    if (!strcmp(tokens[0], "REPORT") && count == 8 && reportFields(tokens + 1, c.report))
    {
        c.action = ConsoleAction::Report;
        return true;
    }
    if (!strcmp(tokens[0], "REVISE") && count == 9 &&
        unsignedNumber(tokens[1], UINT64_MAX, number) && number != 0 && reportFields(tokens + 2, c.report))
    {
        c.action = ConsoleAction::Revise;
        c.id = number;
        return true;
    }
    return false;
}

uint32_t PhoneClock::now(uint32_t tick)
{
    const uint32_t elapsed = tick - lastTick;
    lastTick = tick;
    if (!utc) return 0;
    const uint64_t milliseconds = static_cast<uint64_t>(remainder) + elapsed;
    const uint64_t next = static_cast<uint64_t>(utc) + milliseconds / 1000;
    if (next > UINT32_MAX) { utc = 0; remainder = 0; return 0; }
    utc = static_cast<uint32_t>(next);
    remainder = static_cast<uint32_t>(milliseconds % 1000);
    return utc;
}

bool PhoneClock::set(uint32_t value, uint32_t tick)
{
    const uint32_t current = now(tick);
    if (!value || (current && value < current)) return false;
    utc = value;
    lastTick = tick;
    remainder = 0;
    return true;
}
