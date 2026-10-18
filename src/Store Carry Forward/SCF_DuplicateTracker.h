#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Message.h"

namespace scf
{
class DuplicateTracker
{
public:
    static constexpr size_t CAPACITY = 16;

    enum class RememberResult : uint8_t
    {
        Added,
        Invalid,
        Duplicate,
        Full
    };

    DuplicateTracker();

    RememberResult remember(const BundleId &bundleId);
    bool contains(const BundleId &bundleId) const;
    size_t count() const;
    void clear();

private:
    BundleId history[CAPACITY];
    size_t historyCount;
};
}
