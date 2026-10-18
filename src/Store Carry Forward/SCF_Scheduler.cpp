#include "SCF_Scheduler.h"

namespace scf
{
Scheduler::Scheduler(uint8_t maxRetransmissions, uint32_t ackTimeoutMs)
    : buffer(), reliability(maxRetransmissions, ackTimeoutMs)
{
    for (size_t index = 0; index < Buffer::CAPACITY; ++index)
    {
        retained[index].used = false;
    }

    for (size_t index = 0; index < Reliability::CAPACITY; ++index)
    {
        attempts[index].used = false;
    }
}

Scheduler::EnqueueResult Scheduler::enqueue(const Message &message)
{
    const Buffer::EnqueueResult result = buffer.enqueue(message);
    if (result != Buffer::EnqueueResult::Enqueued)
    {
        return static_cast<EnqueueResult>(result);
    }

    for (size_t index = 0; index < Buffer::CAPACITY; ++index)
    {
        if (!retained[index].used)
        {
            retained[index].bundleId = message.bundleId;
            retained[index].used = true;
            return EnqueueResult::Enqueued;
        }
    }

    buffer.remove(message.bundleId);
    return EnqueueResult::Full;
}

Scheduler::EncounterResult Scheduler::encounter(
    uint32_t peerId, uint32_t now, SendRequest &request)
{
    if (peerId == 0)
    {
        return EncounterResult::InvalidPeer;
    }

    expire(now);

    const Message *selected = selectForPeer(peerId, now);
    if (selected == nullptr)
    {
        return EncounterResult::NoEligibleBundle;
    }

    const Reliability::State currentState =
        reliability.state(selected->bundleId, peerId);
    if (hasAttempt(selected->bundleId, peerId))
    {
        if (currentState != Reliability::State::RetryDue)
        {
            return EncounterResult::NoEligibleBundle;
        }

        uint8_t retransmissionCount = 0;
        for (size_t index = 0; index < Reliability::CAPACITY; ++index)
        {
            if (attempts[index].used && attempts[index].peerId == peerId &&
                sameBundle(attempts[index].bundleId, selected->bundleId))
            {
                retransmissionCount = attempts[index].retransmissionCount;
                break;
            }
        }

        fillRequest(*selected, peerId, true, retransmissionCount, request);
        return EncounterResult::SendReady;
    }

    const Reliability::StartResult startResult =
        reliability.start(*selected, peerId, now);
    if (startResult == Reliability::StartResult::Full)
    {
        return EncounterResult::ReliabilityFull;
    }

    if (startResult != Reliability::StartResult::Waiting ||
        !addAttempt(selected->bundleId, peerId))
    {
        return EncounterResult::NoEligibleBundle;
    }

    fillRequest(*selected, peerId, false, 0, request);
    return EncounterResult::SendReady;
}

Scheduler::TickResult Scheduler::update(uint32_t now, SendRequest &request)
{
    request = {};
    expire(now);

    Reliability::RetryRequest retryRequest = {};
    const Reliability::UpdateResult result = reliability.update(now, retryRequest);
    if (result == Reliability::UpdateResult::Waiting)
    {
        return TickResult::Waiting;
    }

    const Message *message = buffer.find(retryRequest.bundleId);
    if (message == nullptr)
    {
        return result == Reliability::UpdateResult::Failed
                   ? TickResult::Failed
                   : TickResult::Waiting;
    }

    if (result == Reliability::UpdateResult::RetryDue)
    {
        for (size_t index = 0; index < Reliability::CAPACITY; ++index)
        {
            if (attempts[index].used &&
                attempts[index].peerId == retryRequest.peerId &&
                sameBundle(attempts[index].bundleId, retryRequest.bundleId))
            {
                attempts[index].retransmissionCount =
                    retryRequest.retransmissionCount;
                break;
            }
        }

        fillRequest(*message, retryRequest.peerId, true,
                    retryRequest.retransmissionCount, request);
        return TickResult::RetryDue;
    }

    return TickResult::Failed;
}

Reliability::FeedbackResult Scheduler::acknowledge(
    const BundleId &bundleId, uint32_t peerId)
{
    return reliability.acknowledge(bundleId, peerId);
}

Reliability::FeedbackResult Scheduler::reject(
    const BundleId &bundleId, uint32_t peerId)
{
    const Reliability::FeedbackResult result =
        reliability.reject(bundleId, peerId);
    if (result == Reliability::FeedbackResult::RetryDue)
    {
        for (size_t index = 0; index < Reliability::CAPACITY; ++index)
        {
            if (attempts[index].used && attempts[index].peerId == peerId &&
                sameBundle(attempts[index].bundleId, bundleId))
            {
                ++attempts[index].retransmissionCount;
                break;
            }
        }
    }

    return result;
}

bool Scheduler::retrySent(const SendRequest &request, uint32_t sentAt)
{
    return request.retransmission &&
           reliability.retrySent(request.bundleId, request.peerId, sentAt);
}

bool Scheduler::releasePeer(const BundleId &bundleId, uint32_t peerId)
{
    if (!hasAttempt(bundleId, peerId) ||
        !reliability.remove(bundleId, peerId))
    {
        return false;
    }

    clearAttempt(bundleId, peerId);
    return true;
}

bool Scheduler::removeBundle(const BundleId &bundleId)
{
    if (!buffer.remove(bundleId))
    {
        return false;
    }

    for (size_t index = 0; index < Reliability::CAPACITY; ++index)
    {
        if (attempts[index].used && sameBundle(attempts[index].bundleId, bundleId))
        {
            reliability.remove(attempts[index].bundleId, attempts[index].peerId);
            attempts[index].used = false;
        }
    }

    clearRetained(bundleId);
    return true;
}

size_t Scheduler::expire(uint32_t now)
{
    size_t removedCount = 0;
    for (size_t index = 0; index < Buffer::CAPACITY; ++index)
    {
        if (!retained[index].used)
        {
            continue;
        }

        const Message *message = buffer.find(retained[index].bundleId);
        if (message != nullptr && timeReached(now, message->expiresAt))
        {
            removeBundle(message->bundleId);
            ++removedCount;
        }
        else if (message == nullptr)
        {
            retained[index].used = false;
        }
    }

    return removedCount;
}

size_t Scheduler::bundleCount() const
{
    return buffer.count();
}

size_t Scheduler::pendingCount() const
{
    return reliability.count();
}

bool Scheduler::timeReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

bool Scheduler::precedes(const Message &candidate, const Message &current)
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

bool Scheduler::hasAttempt(const BundleId &bundleId, uint32_t peerId) const
{
    for (size_t index = 0; index < Reliability::CAPACITY; ++index)
    {
        if (attempts[index].used && attempts[index].peerId == peerId &&
            sameBundle(attempts[index].bundleId, bundleId))
        {
            return true;
        }
    }

    return false;
}

bool Scheduler::addAttempt(const BundleId &bundleId, uint32_t peerId)
{
    for (size_t index = 0; index < Reliability::CAPACITY; ++index)
    {
        if (!attempts[index].used)
        {
            attempts[index].bundleId = bundleId;
            attempts[index].peerId = peerId;
            attempts[index].retransmissionCount = 0;
            attempts[index].used = true;
            return true;
        }
    }

    return false;
}

void Scheduler::clearAttempt(const BundleId &bundleId, uint32_t peerId)
{
    for (size_t index = 0; index < Reliability::CAPACITY; ++index)
    {
        if (attempts[index].used && attempts[index].peerId == peerId &&
            sameBundle(attempts[index].bundleId, bundleId))
        {
            attempts[index].used = false;
            return;
        }
    }
}

void Scheduler::clearRetained(const BundleId &bundleId)
{
    for (size_t index = 0; index < Buffer::CAPACITY; ++index)
    {
        if (retained[index].used && sameBundle(retained[index].bundleId, bundleId))
        {
            retained[index].used = false;
            return;
        }
    }
}

const Message *Scheduler::selectForPeer(uint32_t peerId, uint32_t now) const
{
    const Message *selected = nullptr;
    for (size_t index = 0; index < Buffer::CAPACITY; ++index)
    {
        if (!retained[index].used)
        {
            continue;
        }

        const Message *candidate = buffer.find(retained[index].bundleId);
        if (candidate == nullptr || timeReached(now, candidate->expiresAt))
        {
            continue;
        }

        if (hasAttempt(candidate->bundleId, peerId))
        {
            const Reliability::State currentState =
                reliability.state(candidate->bundleId, peerId);
            if (currentState != Reliability::State::RetryDue)
            {
                continue;
            }
        }

        if (selected == nullptr || precedes(*candidate, *selected))
        {
            selected = candidate;
        }
    }

    return selected;
}

void Scheduler::fillRequest(const Message &message, uint32_t peerId,
                            bool retransmission, uint8_t retransmissionCount,
                            SendRequest &request) const
{
    request.bundleId = message.bundleId;
    request.peerId = peerId;
    request.message = &message;
    request.retransmission = retransmission;
    request.retransmissionCount = retransmissionCount;
}
}
