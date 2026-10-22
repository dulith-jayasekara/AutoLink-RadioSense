#pragma once
#include "Hazard_Repository.h"

enum class RetentionPhase : uint8_t { Ongoing, UnknownTime, HistoryRetained, Eligible, CapacityBlocked };
struct HazardRetentionView
{
    RetentionPhase phase;
    HazardStatus terminalStatus;
    uint64_t deadline, remainingSeconds;
    bool timeKnown;
};
struct HazardRetentionSummary
{
    size_t fullTerminalCount, eligibleCount, readyCount, blockedCount, markerCount, freeMarkerSlots;
};
// Read-only retention assessment shared by cleanup, diagnostics and application views.
class HazardRetention
{
public:
    static constexpr unsigned POLICY_VERSION = 1;
    static constexpr uint32_t HISTORY_SECONDS = 86400;
    static HazardRetentionView evaluate(const HazardRepository &repo, const HazardRecord &record, uint32_t now);
    static HazardRetentionSummary summarize(const HazardRepository &repo, uint32_t now);
    static const char *phaseName(RetentionPhase phase);
};
