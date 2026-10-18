#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Buffer.h"
#include "SCF_Reliability.h"

namespace scf
{
class Scheduler
{
public:
    enum class EnqueueResult : uint8_t
    {
        Enqueued,
        Invalid,
        Duplicate,
        Full
    };

    enum class EncounterResult : uint8_t
    {
        SendReady,
        NoEligibleBundle,
        InvalidPeer,
        ReliabilityFull
    };

    enum class TickResult : uint8_t
    {
        Waiting,
        RetryDue,
        Failed
    };

    struct SendRequest
    {
        BundleId bundleId;
        uint32_t peerId;
        const Message *message;
        bool retransmission;
        uint8_t retransmissionCount;
    };

    Scheduler(
        uint8_t maxRetransmissions = Reliability::DEFAULT_MAX_RETRANSMISSIONS,
        uint32_t ackTimeoutMs = Reliability::DEFAULT_ACK_TIMEOUT_MS);

    EnqueueResult enqueue(const Message &message);
    EncounterResult encounter(uint32_t peerId, uint32_t now,
                              SendRequest &request);
    TickResult update(uint32_t now, SendRequest &request);
    Reliability::FeedbackResult acknowledge(const BundleId &bundleId,
                                             uint32_t peerId);
    Reliability::FeedbackResult reject(const BundleId &bundleId,
                                       uint32_t peerId);
    bool retrySent(const SendRequest &request, uint32_t sentAt);
    bool releasePeer(const BundleId &bundleId, uint32_t peerId);
    bool removeBundle(const BundleId &bundleId);
    size_t expire(uint32_t now);
    size_t bundleCount() const;
    size_t pendingCount() const;

private:
    struct RetainedBundle
    {
        BundleId bundleId;
        bool used;
    };

    struct Attempt
    {
        BundleId bundleId;
        uint32_t peerId;
        uint8_t retransmissionCount;
        bool used;
    };

    static bool timeReached(uint32_t now, uint32_t deadline);
    static bool precedes(const Message &candidate, const Message &current);
    bool hasAttempt(const BundleId &bundleId, uint32_t peerId) const;
    bool addAttempt(const BundleId &bundleId, uint32_t peerId);
    void clearAttempt(const BundleId &bundleId, uint32_t peerId);
    void clearRetained(const BundleId &bundleId);
    const Message *selectForPeer(uint32_t peerId, uint32_t now) const;
    void fillRequest(const Message &message, uint32_t peerId,
                     bool retransmission, uint8_t retransmissionCount,
                     SendRequest &request) const;

    Buffer buffer;
    Reliability reliability;
    RetainedBundle retained[Buffer::CAPACITY];
    Attempt attempts[Reliability::CAPACITY];
};
}
