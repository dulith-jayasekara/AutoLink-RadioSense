#include "SCF_Reliability.h"

#include <stdint.h>

namespace scf
{
Reliability::Reliability(uint8_t maxRetransmissions, uint32_t ackTimeoutMs)
    : entryCount(0),
      configuredMaxRetransmissions(maxRetransmissions),
      configuredAckTimeoutMs(ackTimeoutMs)
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        entries[index].used = false;
    }
}

Reliability::StartResult Reliability::start(
    const Message &message, uint32_t peerId, uint32_t sentAt)
{
    if (!isValidMessage(message) || peerId == 0 ||
        timeReached(sentAt, message.expiresAt))
    {
        return StartResult::Rejected;
    }

    if (find(message.bundleId, peerId) != nullptr)
    {
        return StartResult::Duplicate;
    }

    if (!hasCapacity())
    {
        return StartResult::Full;
    }

    for (size_t index = 0; index < CAPACITY; ++index)
    {
        if (!entries[index].used)
        {
            entries[index].message = message;
            entries[index].peerId = peerId;
            entries[index].deadline = sentAt + configuredAckTimeoutMs;
            entries[index].retransmissionCount = 0;
            entries[index].state = State::Waiting;
            entries[index].used = true;
            ++entryCount;
            return StartResult::Waiting;
        }
    }

    return StartResult::Full;
}

Reliability::FeedbackResult Reliability::acknowledge(
    const BundleId &bundleId, uint32_t peerId)
{
    Entry *entry = find(bundleId, peerId);
    if (entry == nullptr)
    {
        if (findBundle(bundleId) != nullptr)
        {
            return FeedbackResult::WrongPeer;
        }

        return FeedbackResult::WrongBundle;
    }

    if (entry->state == State::Acknowledged)
    {
        return FeedbackResult::DuplicateAck;
    }

    if (entry->state == State::Failed)
    {
        return FeedbackResult::Ignored;
    }

    entry->state = State::Acknowledged;
    return FeedbackResult::Acknowledged;
}

Reliability::FeedbackResult Reliability::reject(
    const BundleId &bundleId, uint32_t peerId)
{
    Entry *entry = find(bundleId, peerId);
    if (entry == nullptr)
    {
        if (findBundle(bundleId) != nullptr)
        {
            return FeedbackResult::WrongPeer;
        }

        return FeedbackResult::WrongBundle;
    }

    if (entry->state == State::Acknowledged || entry->state == State::Failed)
    {
        return FeedbackResult::Ignored;
    }

    if (entry->retransmissionCount >= configuredMaxRetransmissions)
    {
        entry->state = State::Failed;
        return FeedbackResult::Failed;
    }

    ++entry->retransmissionCount;
    entry->state = State::RetryDue;
    return FeedbackResult::RetryDue;
}

Reliability::UpdateResult Reliability::update(uint32_t now, RetryRequest &request)
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        Entry &entry = entries[index];
        if (!entry.used || entry.state != State::Waiting)
        {
            continue;
        }

        if (timeReached(now, entry.message.expiresAt))
        {
            entry.state = State::Failed;
            fillRetryRequest(entry, request);
            return UpdateResult::Failed;
        }

        if (!timeReached(now, entry.deadline))
        {
            continue;
        }

        if (entry.retransmissionCount >= configuredMaxRetransmissions)
        {
            entry.state = State::Failed;
            fillRetryRequest(entry, request);
            return UpdateResult::Failed;
        }

        ++entry.retransmissionCount;
        entry.state = State::RetryDue;
        fillRetryRequest(entry, request);
        return UpdateResult::RetryDue;
    }

    return UpdateResult::Waiting;
}

bool Reliability::retrySent(
    const BundleId &bundleId, uint32_t peerId, uint32_t sentAt)
{
    Entry *entry = find(bundleId, peerId);
    if (entry == nullptr || entry->state != State::RetryDue ||
        timeReached(sentAt, entry->message.expiresAt))
    {
        return false;
    }

    entry->deadline = sentAt + configuredAckTimeoutMs;
    entry->state = State::Waiting;
    return true;
}

Reliability::State Reliability::state(
    const BundleId &bundleId, uint32_t peerId) const
{
    const Entry *entry = find(bundleId, peerId);
    return entry == nullptr ? State::Failed : entry->state;
}

bool Reliability::remove(const BundleId &bundleId, uint32_t peerId)
{
    Entry *entry = find(bundleId, peerId);
    if (entry == nullptr)
    {
        return false;
    }

    entry->used = false;
    --entryCount;
    return true;
}

size_t Reliability::count() const
{
    return entryCount;
}

uint8_t Reliability::maxRetransmissions() const
{
    return configuredMaxRetransmissions;
}

uint32_t Reliability::ackTimeoutMs() const
{
    return configuredAckTimeoutMs;
}

bool Reliability::timeReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

Reliability::Entry *Reliability::find(
    const BundleId &bundleId, uint32_t peerId)
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        if (entries[index].used && entries[index].peerId == peerId &&
            sameBundle(entries[index].message.bundleId, bundleId))
        {
            return &entries[index];
        }
    }

    return nullptr;
}

const Reliability::Entry *Reliability::find(
    const BundleId &bundleId, uint32_t peerId) const
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        if (entries[index].used && entries[index].peerId == peerId &&
            sameBundle(entries[index].message.bundleId, bundleId))
        {
            return &entries[index];
        }
    }

    return nullptr;
}

Reliability::Entry *Reliability::findBundle(const BundleId &bundleId)
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        if (entries[index].used &&
            sameBundle(entries[index].message.bundleId, bundleId))
        {
            return &entries[index];
        }
    }

    return nullptr;
}

const Reliability::Entry *Reliability::findBundle(const BundleId &bundleId) const
{
    for (size_t index = 0; index < CAPACITY; ++index)
    {
        if (entries[index].used &&
            sameBundle(entries[index].message.bundleId, bundleId))
        {
            return &entries[index];
        }
    }

    return nullptr;
}

bool Reliability::hasCapacity() const
{
    return entryCount < CAPACITY;
}

void Reliability::fillRetryRequest(
    const Entry &entry, RetryRequest &request) const
{
    request.bundleId = entry.message.bundleId;
    request.peerId = entry.peerId;
    request.message = &entry.message;
    request.retransmissionCount = entry.retransmissionCount;
}
}
