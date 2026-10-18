#include "SCF_DuplicateTracker.h"

namespace scf
{
DuplicateTracker::DuplicateTracker()
    : historyCount(0)
{
}

DuplicateTracker::RememberResult DuplicateTracker::remember(const BundleId &bundleId)
{
    if (!isValidBundleId(bundleId))
    {
        return RememberResult::Invalid;
    }

    if (contains(bundleId))
    {
        return RememberResult::Duplicate;
    }

    if (historyCount >= CAPACITY)
    {
        return RememberResult::Full;
    }

    history[historyCount++] = bundleId;
    return RememberResult::Added;
}

bool DuplicateTracker::contains(const BundleId &bundleId) const
{
    for (size_t index = 0; index < historyCount; ++index)
    {
        if (sameBundle(history[index], bundleId))
        {
            return true;
        }
    }

    return false;
}

size_t DuplicateTracker::count() const
{
    return historyCount;
}

void DuplicateTracker::clear()
{
    historyCount = 0;
}
}
