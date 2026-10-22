#include "Hazard_Memory.h"
#include "Hazard_Confidence.h"
#include "Hazard_Lifecycle.h"

unsigned HazardMemory::priority(const HazardRepository &repo, const HazardRecord &r, uint32_t now,
                                bool positionKnown, double latitude, double longitude)
{
    if (HazardLifecycle::evaluate(r, now) == HazardStatus::Resolved ||
        HazardLifecycle::evaluate(r, now) == HazardStatus::Expired) return 0;
    const uint32_t lifetime = r.expiresAt - r.lastObservedAt;
    const uint32_t age = now && now >= r.lastObservedAt ? now - r.lastObservedAt : 0;
    const unsigned freshness = age >= lifetime ? 0 :
        static_cast<unsigned>((static_cast<uint64_t>(lifetime - age) * 100) / lifetime);
    unsigned relevance = 0;
    if (positionKnown)
    {
        const double distance = hazardDistanceMeters(latitude, longitude, r.latitude, r.longitude);
        if (distance < 5000) relevance = static_cast<unsigned>((5000 - distance) / 50);
    }
    return r.severity * 100u + HazardConfidence::evaluate(repo, r, now).score * 2u + freshness + relevance;
}
size_t HazardMemory::cleanup(HazardRepository &repo, uint32_t now)
{
    if (!now) return 0;
    size_t removed = 0;
    for (size_t i = 0; i < repo.count();)
    {
        HazardRecord r; repo.getAt(i, r);
        const HazardRetentionView view = HazardRetention::evaluate(repo, r, now);
        if (view.phase == RetentionPhase::Eligible &&
            repo.retire(HazardRetirement{r.id, r.createdAt, view.terminalStatus})) ++removed;
        else ++i;
    }
    return removed;
}
namespace
{
bool pinned(const HazardRepository &repo, const HazardRecord &r, uint32_t now, uint32_t localNode)
{
    if (r.status == HazardStatus::Resolved || r.status == HazardStatus::Expired || now >= r.expiresAt) return true;
    for (size_t i = 0; i < repo.observationCount(); ++i)
    {
        HazardObservation o; repo.observationAt(i, o);
        if (o.hazardId == r.id && o.observerNodeId == localNode && o.clientReportId &&
            (now < o.observedAt || now - o.observedAt <= HazardMemory::RETRY_PROTECTION_SECONDS)) return true;
    }
    return false;
}
}
bool HazardMemory::makeRoom(HazardRepository &repo, const HazardRecord &input, uint32_t now,
                            uint32_t localNode, bool needsRecord, bool needsEvidence,
                            bool positionKnown, double latitude, double longitude)
{
    const HazardRecord incoming = input; // A caller may pass a record inside the mutable table.
    while ((needsRecord && repo.count() == HazardRepository::CAPACITY) ||
           (needsEvidence && repo.observationCount() == HazardRepository::EVIDENCE_CAPACITY))
    {
        if (!now || now < incoming.lastObservedAt) return false;
        const unsigned incomingPriority = priority(repo, incoming, now, positionKnown, latitude, longitude);
        bool found = false; HazardRecord victim = {}; unsigned lowest = incomingPriority;
        for (size_t i = 0; i < repo.count(); ++i)
        {
            HazardRecord r; repo.getAt(i, r);
            if (r.id == incoming.id || pinned(repo, r, now, localNode)) continue;
            // An evidence-only shortage requires a victim that actually frees evidence.
            bool hasEvidence = false;
            for (size_t j = 0; j < repo.observationCount(); ++j)
            { HazardObservation o; repo.observationAt(j, o); if (o.hazardId == r.id) hasEvidence = true; }
            if (needsEvidence && repo.observationCount() == HazardRepository::EVIDENCE_CAPACITY && !hasEvidence) continue;
            const unsigned score = priority(repo, r, now, positionKnown, latitude, longitude);
            if (score < lowest || (found && score == lowest &&
                (r.lastObservedAt < victim.lastObservedAt ||
                 (r.lastObservedAt == victim.lastObservedAt && r.id > victim.id))))
            { lowest = score; victim = r; found = true; }
        }
        if (!found) return false;
        repo.remove(victim.id); repo.removeEvidence(victim.id);
    }
    return true;
}
