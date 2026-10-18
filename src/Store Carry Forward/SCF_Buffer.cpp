#include "SCF_Buffer.h"

namespace scf
{
namespace
{
bool precedes(const Message &candidate, const Message &current)
{
    if (candidate.priority != current.priority)
    {
        return static_cast<uint8_t>(candidate.priority) >
               static_cast<uint8_t>(current.priority);
    }

    if (candidate.expiresAt != current.expiresAt)
    {
        return candidate.expiresAt < current.expiresAt;
    }

    if (candidate.bundleId.sourceId != current.bundleId.sourceId)
    {
        return candidate.bundleId.sourceId < current.bundleId.sourceId;
    }

    return candidate.bundleId.sequence < current.bundleId.sequence;
}
}

Buffer::Buffer()
    : messageCount(0)
{
}

Buffer::EnqueueResult Buffer::enqueue(const Message &message)
{
    if (!isValidMessage(message))
    {
        return EnqueueResult::Invalid;
    }

    if (find(message.bundleId) != nullptr)
    {
        return EnqueueResult::Duplicate;
    }

    if (messageCount >= CAPACITY)
    {
        return EnqueueResult::Full;
    }

    messages[messageCount++] = message;
    return EnqueueResult::Enqueued;
}

const Message *Buffer::find(const BundleId &bundleId) const
{
    for (size_t index = 0; index < messageCount; ++index)
    {
        if (sameBundle(messages[index].bundleId, bundleId))
        {
            return &messages[index];
        }
    }

    return nullptr;
}

const Message *Buffer::selectNext(uint32_t now) const
{
    const Message *selected = nullptr;

    for (size_t index = 0; index < messageCount; ++index)
    {
        const Message &candidate = messages[index];
        if (candidate.expiresAt <= now)
        {
            continue;
        }

        if (selected == nullptr || precedes(candidate, *selected))
        {
            selected = &candidate;
        }
    }

    return selected;
}

size_t Buffer::expire(uint32_t now)
{
    size_t removedCount = 0;
    size_t index = 0;

    while (index < messageCount)
    {
        if (messages[index].expiresAt <= now)
        {
            messages[index] = messages[messageCount - 1];
            --messageCount;
            ++removedCount;
        }
        else
        {
            ++index;
        }
    }

    return removedCount;
}

bool Buffer::remove(const BundleId &bundleId)
{
    for (size_t index = 0; index < messageCount; ++index)
    {
        if (sameBundle(messages[index].bundleId, bundleId))
        {
            messages[index] = messages[messageCount - 1];
            --messageCount;
            return true;
        }
    }

    return false;
}

size_t Buffer::count() const
{
    return messageCount;
}

bool Buffer::empty() const
{
    return messageCount == 0;
}
}
