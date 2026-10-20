#include "Hazard_Groups.h"

namespace
{
const HazardObservation *anchor(const HazardRepository &repo, HazardId id)
{
    const HazardObservation *o = repo.findObservation(id);
    return o && o->kind == ObservationKind::Report && o->hazardId == id ? o : nullptr;
}
bool member(const HazardGroup &g, HazardId id)
{
    for (size_t i = 0; i < g.memberCount; ++i) if (g.members[i] == id) return true;
    return false;
}
}
bool HazardGroups::matches(const HazardObservation &a, const HazardObservation &b)
{
    if (!HazardEvidenceCodec::valid(a) || !HazardEvidenceCodec::valid(b) ||
        a.kind != ObservationKind::Report || b.kind != ObservationKind::Report ||
        a.type != b.type || a.locationAccuracyMeters > MATCH_DISTANCE_METERS ||
        b.locationAccuracyMeters > MATCH_DISTANCE_METERS) return false;
    const uint32_t time = a.observedAt > b.observedAt ? a.observedAt - b.observedAt : b.observedAt - a.observedAt;
    return time <= MATCH_TIME_SECONDS &&
        hazardDistanceMeters(a.latitude, a.longitude, b.latitude, b.longitude) <= MATCH_DISTANCE_METERS;
}
size_t HazardGroups::build(const HazardRepository &repo, HazardGroup *output, size_t capacity)
{
    HazardId ids[HazardRepository::CAPACITY] = {};
    const size_t count = repo.count();
    for (size_t i = 0; i < count; ++i)
    {
        HazardRecord r; repo.getAt(i, r); ids[i] = r.id;
        size_t j = i;
        while (j && ids[j] < ids[j - 1]) { const HazardId t = ids[j]; ids[j] = ids[j - 1]; ids[j - 1] = t; --j; }
    }
    HazardGroup groups[HazardRepository::CAPACITY] = {};
    size_t groupCount = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const HazardObservation *observation = anchor(repo, ids[i]);
        size_t choice = groupCount;
        for (size_t g = 0; observation && g < groupCount; ++g)
        {
            bool compatible = true;
            for (size_t m = 0; m < groups[g].memberCount; ++m)
            {
                const HazardObservation *other = anchor(repo, groups[g].members[m]);
                if (!other || !matches(*observation, *other)) { compatible = false; break; }
            }
            if (compatible) { choice = g; break; }
        }
        if (choice == groupCount) { groups[groupCount].canonicalId = ids[i]; ++groupCount; }
        HazardGroup &group = groups[choice];
        group.members[group.memberCount++] = ids[i];
    }
    for (size_t g = 0; g < groupCount; ++g)
    {
        HazardGroup &group = groups[g];
        uint32_t observers[HazardRepository::EVIDENCE_CAPACITY] = {};
        size_t observersCount = 0;
        for (size_t i = 0; i < repo.observationCount(); ++i)
        {
            HazardObservation o; repo.observationAt(i, o);
            if (!member(group, o.hazardId)) continue;
            ++group.observationCount;
            if (o.kind == ObservationKind::Clearance || o.kind == ObservationKind::Contradiction) continue;
            bool seen = false;
            for (size_t j = 0; j < observersCount; ++j) if (observers[j] == o.observerNodeId) seen = true;
            if (!seen) observers[observersCount++] = o.observerNodeId;
        }
        group.independentObservers = observersCount;
        bool terminal = false, ongoing = false;
        for (size_t i = 0; i < group.memberCount; ++i)
        {
            const HazardRecord *r = repo.findById(group.members[i]);
            if (r->status == HazardStatus::Resolved || r->status == HazardStatus::Expired) terminal = true;
            else ongoing = true;
        }
        group.mixedTerminalStates = terminal && ongoing;
        if (output && g < capacity) output[g] = group;
    }
    return groupCount;
}
