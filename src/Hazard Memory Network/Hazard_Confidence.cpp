#include "Hazard_Confidence.h"
#include "Hazard_Groups.h"
#include "Hazard_Lifecycle.h"

namespace
{
bool contains(const HazardGroup &group, HazardId id)
{
    for (size_t i = 0; i < group.memberCount; ++i) if (group.members[i] == id) return true;
    return false;
}
bool negative(ObservationKind kind)
{
    return kind == ObservationKind::Contradiction || kind == ObservationKind::Clearance;
}
struct Vote { uint32_t observer, at; uint64_t id; bool against; };
}

HazardConfidenceResult HazardConfidence::evaluate(const HazardRepository &repo,
                                                 const HazardRecord &record, uint32_t now)
{
    HazardConfidenceResult result = {};
    uint32_t age = 0;
    result.timeKnown = HazardLifecycle::ageSeconds(record, now, age);
    HazardGroup groups[HazardRepository::CAPACITY];
    const size_t count = HazardGroups::build(repo, groups, HazardRepository::CAPACITY);
    const HazardGroup *group = nullptr;
    for (size_t i = 0; i < count; ++i) if (contains(groups[i], record.id)) { group = &groups[i]; break; }
    Vote votes[HazardRepository::EVIDENCE_CAPACITY] = {};
    size_t voteCount = 0;
    for (size_t i = 0; i < repo.observationCount(); ++i)
    {
        HazardObservation o; repo.observationAt(i, o);
        if (o.hazardId != record.id && (!group || !contains(*group, o.hazardId))) continue;
        if (o.type != record.type || o.locationAccuracyMeters > MAX_DISTANCE_METERS ||
            hazardDistanceMeters(o.latitude, o.longitude, record.latitude, record.longitude) > MAX_DISTANCE_METERS ||
            o.observedAt >= record.expiresAt || (result.timeKnown && o.observedAt > now)) continue;
        // Controlled clearance events retain the original fix; fresh GPS is required for other votes.
        if (o.kind != ObservationKind::Clearance &&
            o.observedAt - o.locationFixAt > REPORT_WINDOW_SECONDS) continue;
        const bool against = negative(o.kind);
        if (o.kind == ObservationKind::Report || o.kind == ObservationKind::Revision)
        {
            const uint32_t gap = o.observedAt > record.lastObservedAt ?
                o.observedAt - record.lastObservedAt : record.lastObservedAt - o.observedAt;
            if (gap > REPORT_WINDOW_SECONDS) continue;
        }
        else if (o.observedAt < record.lastObservedAt) continue;
        size_t slot = 0;
        while (slot < voteCount && votes[slot].observer != o.observerNodeId) ++slot;
        if (slot == voteCount)
            votes[voteCount++] = Vote{o.observerNodeId, o.observedAt, o.id, against};
        else
        {
            const Vote &old = votes[slot];
            // Timestamp wins; a contradiction wins equal timestamps regardless of arrival order.
            if (o.observedAt > old.at || (o.observedAt == old.at &&
                ((against && !old.against) || (against == old.against && o.id > old.id))))
                votes[slot] = Vote{o.observerNodeId, o.observedAt, o.id, against};
        }
    }
    for (size_t i = 0; i < voteCount; ++i)
        if (votes[i].against) ++result.contradictingObservers; else ++result.supportingObservers;
    const size_t additional = result.supportingObservers ? result.supportingObservers - 1 : 0;
    int score = INITIAL_SCORE + SUPPORT_WEIGHT * static_cast<int>(additional) -
        CONTRADICTION_WEIGHT * static_cast<int>(result.contradictingObservers);
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    result.evidenceScore = static_cast<uint8_t>(score);
    if (result.timeKnown)
    {
        const uint32_t lifetime = record.expiresAt - record.lastObservedAt;
        uint64_t quarters = static_cast<uint64_t>(age) * 4 / lifetime;
        if (quarters > 4) quarters = 4;
        result.decayPenalty = static_cast<uint8_t>(quarters * DECAY_PER_QUARTER);
    }
    result.score = score > result.decayPenalty ? static_cast<uint8_t>(score - result.decayPenalty) : 0;
    HazardRecord view = record;
    view.confidence = result.score;
    result.effectiveStatus = HazardLifecycle::evaluate(view, now);
    if (result.effectiveStatus == HazardStatus::Resolved || result.effectiveStatus == HazardStatus::Expired)
        result.score = 0;
    return result;
}

const char *HazardConfidence::level(uint8_t score)
{
    return score >= HazardLifecycle::ACTIVE_CONFIDENCE ? "High" : (score >= 40 ? "Medium" : "Low");
}
