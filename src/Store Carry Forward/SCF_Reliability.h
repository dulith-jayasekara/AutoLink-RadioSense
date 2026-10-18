#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Message.h"

namespace scf
{
class Reliability
{
public:
    static constexpr size_t CAPACITY = 8;
    static constexpr uint8_t DEFAULT_MAX_RETRANSMISSIONS = 3;
    static constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS = 1000;

    enum class StartResult : uint8_t
    {
        Waiting,
        Rejected,
        Duplicate,
        Full
    };

    enum class State : uint8_t
    {
        Waiting,
        RetryDue,
        Acknowledged,
        Failed
    };

    enum class FeedbackResult : uint8_t
    {
        Acknowledged,
        RetryDue,
        Failed,
        WrongPeer,
        WrongBundle,
        DuplicateAck,
        Ignored
    };

    enum class UpdateResult : uint8_t
    {
        Waiting,
        RetryDue,
        Failed
    };

    struct RetryRequest
    {
        BundleId bundleId;
        uint32_t peerId;
        const Message *message;
        uint8_t retransmissionCount;
    };

    Reliability(
        uint8_t maxRetransmissions = DEFAULT_MAX_RETRANSMISSIONS,
        uint32_t ackTimeoutMs = DEFAULT_ACK_TIMEOUT_MS);

    StartResult start(const Message &message, uint32_t peerId, uint32_t sentAt);
    FeedbackResult acknowledge(const BundleId &bundleId, uint32_t peerId);
    FeedbackResult reject(const BundleId &bundleId, uint32_t peerId);
    UpdateResult update(uint32_t now, RetryRequest &request);
    bool retrySent(const BundleId &bundleId, uint32_t peerId, uint32_t sentAt);
    State state(const BundleId &bundleId, uint32_t peerId) const;
    bool remove(const BundleId &bundleId, uint32_t peerId);
    size_t count() const;
    uint8_t maxRetransmissions() const;
    uint32_t ackTimeoutMs() const;

private:
    struct Entry
    {
        Message message;
        uint32_t peerId;
        uint32_t deadline;
        uint8_t retransmissionCount;
        State state;
        bool used;
    };

    static bool timeReached(uint32_t now, uint32_t deadline);
    Entry *find(const BundleId &bundleId, uint32_t peerId);
    const Entry *find(const BundleId &bundleId, uint32_t peerId) const;
    Entry *findBundle(const BundleId &bundleId);
    const Entry *findBundle(const BundleId &bundleId) const;
    bool hasCapacity() const;
    void fillRetryRequest(const Entry &entry, RetryRequest &request) const;

    Entry entries[CAPACITY];
    size_t entryCount;
    uint8_t configuredMaxRetransmissions;
    uint32_t configuredAckTimeoutMs;
};
}
