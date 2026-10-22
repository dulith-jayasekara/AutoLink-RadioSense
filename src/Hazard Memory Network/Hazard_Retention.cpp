#include "Hazard_Retention.h"
#include "Hazard_Lifecycle.h"

HazardRetentionView HazardRetention::evaluate(const HazardRepository &repo, const HazardRecord &r, uint32_t now)
{
    HazardRetentionView view = {};
    view.terminalStatus = HazardLifecycle::evaluate(r, now);
    view.timeKnown = now != 0;
    view.deadline = static_cast<uint64_t>(r.expiresAt) + HISTORY_SECONDS;
    view.remainingSeconds = now && view.deadline > now ? view.deadline - now : 0;
    const bool terminal = view.terminalStatus == HazardStatus::Resolved || view.terminalStatus == HazardStatus::Expired;
    if (!now) view.phase = RetentionPhase::UnknownTime;
    else if (!terminal) view.phase = RetentionPhase::Ongoing;
    else if (static_cast<uint64_t>(now) < view.deadline) view.phase = RetentionPhase::HistoryRetained;
    else view.phase = repo.retirementCount() < HazardRepository::RETIREMENT_CAPACITY ?
        RetentionPhase::Eligible : RetentionPhase::CapacityBlocked;
    return view;
}
HazardRetentionSummary HazardRetention::summarize(const HazardRepository &repo, uint32_t now)
{
    HazardRetentionSummary result = {};
    result.markerCount = repo.retirementCount();
    result.freeMarkerSlots = HazardRepository::RETIREMENT_CAPACITY - result.markerCount;
    for (size_t i = 0; i < repo.count(); ++i)
    {
        HazardRecord r; repo.getAt(i, r);
        const HazardRetentionView view = evaluate(repo, r, now);
        if (view.terminalStatus == HazardStatus::Resolved || view.terminalStatus == HazardStatus::Expired)
            ++result.fullTerminalCount;
        if (view.phase == RetentionPhase::Eligible || view.phase == RetentionPhase::CapacityBlocked) ++result.eligibleCount;
    }
    result.readyCount = result.eligibleCount < result.freeMarkerSlots ? result.eligibleCount : result.freeMarkerSlots;
    result.blockedCount = result.eligibleCount - result.readyCount;
    return result;
}
const char *HazardRetention::phaseName(RetentionPhase phase)
{
    switch (phase)
    {
    case RetentionPhase::Ongoing: return "Ongoing";
    case RetentionPhase::UnknownTime: return "UnknownTime";
    case RetentionPhase::HistoryRetained: return "HistoryRetained";
    case RetentionPhase::Eligible: return "Eligible";
    case RetentionPhase::CapacityBlocked: return "CapacityBlocked";
    }
    return "Unknown";
}
