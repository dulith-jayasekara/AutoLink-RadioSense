#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Codec.h"
#include "SCF_DuplicateTracker.h"
#include "SCF_Scheduler.h"

namespace scf
{
static constexpr uint32_t BROADCAST_ID = 0xFFFFFFFFU;
static constexpr size_t RADIO_FRAME_CAPACITY = MAX_DATA_FRAME_SIZE;

enum class PortReceiveResult : uint8_t
{
    NoPacket,
    Packet,
    Overflow,
    Error
};

class RadioPort
{
public:
    virtual ~RadioPort() = default;
    virtual bool initialize() = 0;
    virtual PortReceiveResult receive(uint8_t *frame, size_t capacity,
                                      size_t &length) = 0;
    virtual bool transmit(const uint8_t *frame, size_t length) = 0;
};

class RadioAdapter
{
public:
    enum class InitResult : uint8_t
    {
        Initialized,
        Failed,
        AlreadyInitialized
    };

    enum class PollResult : uint8_t
    {
        NotInitialized,
        NoPacket,
        DataAccepted,
        DuplicateData,
        AckReady,
        NackReady,
        FeedbackHandled,
        MalformedFrame,
        Overflow,
        RadioError,
        Rejected
    };

    enum class PrepareResult : uint8_t
    {
        NotInitialized,
        Ready,
        NoEligibleBundle,
        InvalidPeer,
        ReliabilityFull,
        Expired,
        Malformed
    };

    enum class TransmitResult : uint8_t
    {
        NotInitialized,
        Sent,
        RadioFailure,
        InvalidFrame
    };

    enum class TickResult : uint8_t
    {
        NotInitialized,
        Waiting,
        RetryReady,
        Failed,
        Expired
    };

    struct OutgoingFrame
    {
        uint8_t bytes[RADIO_FRAME_CAPACITY];
        size_t length;
        uint32_t peerId;
        bool retransmission;
        bool valid;
        Scheduler::SendRequest request;
    };

    RadioAdapter(RadioPort &port, uint32_t localNodeId,
                 uint8_t maxRetransmissions = Reliability::DEFAULT_MAX_RETRANSMISSIONS,
                 uint32_t ackTimeoutMs = Reliability::DEFAULT_ACK_TIMEOUT_MS);

    InitResult initialize();
    PollResult poll(uint32_t now, OutgoingFrame &response);
    PrepareResult prepareEncounter(uint32_t peerId, uint32_t now,
                                   OutgoingFrame &frame);
    TickResult tick(uint32_t now, OutgoingFrame &frame);
    TransmitResult transmit(OutgoingFrame &frame, uint32_t sentAt);

    Scheduler::EnqueueResult enqueue(const Message &message);
    size_t bundleCount() const;
    size_t pendingCount() const;
    bool releasePeer(const BundleId &bundleId, uint32_t peerId);
    bool removeBundle(const BundleId &bundleId);

private:
    static bool timeReached(uint32_t now, uint32_t deadline);
    void clearFrame(OutgoingFrame &frame) const;
    bool prepareFeedback(uint8_t type, const BundleId &bundleId,
                         uint32_t peerId, uint8_t reason,
                         OutgoingFrame &frame) const;
    PollResult handleData(uint32_t now, size_t length,
                          OutgoingFrame &response);
    PollResult handleFeedback(size_t length, OutgoingFrame &response);

    RadioPort &port;
    uint32_t localNodeId;
    bool initialized;
    Scheduler scheduler;
    DuplicateTracker duplicateTracker;
    uint8_t receiveFrame[RADIO_FRAME_CAPACITY];
};
}
