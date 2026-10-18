#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Message.h"

namespace scf
{
class Buffer
{
public:
    static constexpr size_t CAPACITY = 8;

    enum class EnqueueResult : uint8_t
    {
        Enqueued,
        Invalid,
        Duplicate,
        Full
    };

    Buffer();

    EnqueueResult enqueue(const Message &message);
    const Message *find(const BundleId &bundleId) const;
    const Message *selectNext(uint32_t now) const;
    size_t expire(uint32_t now);
    bool remove(const BundleId &bundleId);
    size_t count() const;
    bool empty() const;

private:
    Message messages[CAPACITY];
    size_t messageCount;
};
}
