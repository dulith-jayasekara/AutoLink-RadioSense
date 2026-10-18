#include "SCF_Radio_Adapter.h"

namespace scf
{
RadioAdapter::RadioAdapter(RadioPort &radioPort, uint32_t nodeId,
                           uint8_t maxRetransmissions, uint32_t ackTimeoutMs)
    : port(radioPort),
      localNodeId(nodeId),
      initialized(false),
      scheduler(maxRetransmissions, ackTimeoutMs),
      duplicateTracker()
{
}

RadioAdapter::InitResult RadioAdapter::initialize()
{
    if (initialized)
    {
        return InitResult::AlreadyInitialized;
    }

    if (localNodeId == 0 || !port.initialize())
    {
        return InitResult::Failed;
    }

    initialized = true;
    return InitResult::Initialized;
}

RadioAdapter::PollResult RadioAdapter::poll(
    uint32_t now, OutgoingFrame &response)
{
    clearFrame(response);
    if (!initialized)
    {
        return PollResult::NotInitialized;
    }

    size_t length = 0;
    const PortReceiveResult receiveResult =
        port.receive(receiveFrame, sizeof(receiveFrame), length);
    if (receiveResult == PortReceiveResult::NoPacket)
    {
        return PollResult::NoPacket;
    }
    if (receiveResult == PortReceiveResult::Overflow)
    {
        return PollResult::Overflow;
    }
    if (receiveResult == PortReceiveResult::Error)
    {
        return PollResult::RadioError;
    }
    if (length < FRAME_HEADER_SIZE || length > RADIO_FRAME_CAPACITY)
    {
        return PollResult::MalformedFrame;
    }

    if (receiveFrame[2] == DATA_FRAME_TYPE)
    {
        return handleData(now, length, response);
    }
    if (receiveFrame[2] == ACK_FRAME_TYPE || receiveFrame[2] == NACK_FRAME_TYPE)
    {
        return handleFeedback(length, response);
    }

    return PollResult::MalformedFrame;
}

RadioAdapter::PrepareResult RadioAdapter::prepareEncounter(
    uint32_t peerId, uint32_t now, OutgoingFrame &frame)
{
    clearFrame(frame);
    if (!initialized)
    {
        return PrepareResult::NotInitialized;
    }

    Scheduler::SendRequest request = {};
    const Scheduler::EncounterResult result =
        scheduler.encounter(peerId, now, request);
    if (result == Scheduler::EncounterResult::NoEligibleBundle)
    {
        return PrepareResult::NoEligibleBundle;
    }
    if (result == Scheduler::EncounterResult::InvalidPeer)
    {
        return PrepareResult::InvalidPeer;
    }
    if (result == Scheduler::EncounterResult::ReliabilityFull)
    {
        return PrepareResult::ReliabilityFull;
    }
    if (request.message == nullptr ||
        request.message->hopCount >= request.message->hopLimit)
    {
        return PrepareResult::Malformed;
    }

    frame.message = *request.message;
    if (localNodeId != request.message->bundleId.sourceId)
    {
        ++frame.message.hopCount;
    }
    if (!encodeData(frame.message, request.peerId, frame.bytes,
                    sizeof(frame.bytes), frame.length))
    {
        return PrepareResult::Malformed;
    }

    frame.peerId = request.peerId;
    frame.retransmission = request.retransmission;
    frame.request = request;
    frame.valid = true;
    return PrepareResult::Ready;
}

RadioAdapter::TickResult RadioAdapter::tick(
    uint32_t now, OutgoingFrame &frame)
{
    clearFrame(frame);
    if (!initialized)
    {
        return TickResult::NotInitialized;
    }

    Scheduler::SendRequest request = {};
    const Scheduler::TickResult result = scheduler.update(now, request);
    if (result == Scheduler::TickResult::Waiting)
    {
        return TickResult::Waiting;
    }
    if (result == Scheduler::TickResult::Failed)
    {
        return TickResult::Failed;
    }
    if (request.message == nullptr ||
        request.message->hopCount >= request.message->hopLimit)
    {
        return TickResult::Failed;
    }

    frame.message = *request.message;
    if (localNodeId != request.message->bundleId.sourceId)
    {
        ++frame.message.hopCount;
    }
    if (!encodeData(frame.message, request.peerId, frame.bytes,
                    sizeof(frame.bytes), frame.length))
    {
        return TickResult::Failed;
    }

    frame.peerId = request.peerId;
    frame.retransmission = true;
    frame.request = request;
    frame.valid = true;
    return TickResult::RetryReady;
}

RadioAdapter::TransmitResult RadioAdapter::transmit(
    OutgoingFrame &frame, uint32_t sentAt)
{
    if (!initialized)
    {
        return TransmitResult::NotInitialized;
    }
    if (!frame.valid || frame.length == 0 || frame.length > RADIO_FRAME_CAPACITY)
    {
        return TransmitResult::InvalidFrame;
    }
    if (!port.transmit(frame.bytes, frame.length))
    {
        return TransmitResult::RadioFailure;
    }

    if (frame.retransmission &&
        !scheduler.retrySent(frame.request, sentAt))
    {
        frame.valid = false;
        return TransmitResult::InvalidFrame;
    }

    frame.valid = false;
    return TransmitResult::Sent;
}

Scheduler::EnqueueResult RadioAdapter::enqueue(const Message &message)
{
    return scheduler.enqueue(message);
}

size_t RadioAdapter::bundleCount() const
{
    return scheduler.bundleCount();
}

size_t RadioAdapter::pendingCount() const
{
    return scheduler.pendingCount();
}

bool RadioAdapter::releasePeer(const BundleId &bundleId, uint32_t peerId)
{
    return scheduler.releasePeer(bundleId, peerId);
}

bool RadioAdapter::removeBundle(const BundleId &bundleId)
{
    return scheduler.removeBundle(bundleId);
}

bool RadioAdapter::timeReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

void RadioAdapter::clearFrame(OutgoingFrame &frame) const
{
    frame.length = 0;
    frame.peerId = 0;
    frame.retransmission = false;
    frame.valid = false;
    frame.message = {};
    frame.request = {};
}

bool RadioAdapter::prepareFeedback(uint8_t type, const BundleId &bundleId,
                                   uint32_t peerId, uint8_t reason,
                                   OutgoingFrame &frame) const
{
    bool encoded = false;
    if (type == ACK_FRAME_TYPE)
    {
        encoded = encodeAck(bundleId, localNodeId, frame.bytes,
                            sizeof(frame.bytes), frame.length);
    }
    else if (type == NACK_FRAME_TYPE)
    {
        encoded = encodeNack(bundleId, localNodeId,
                             static_cast<NackReason>(reason), frame.bytes,
                             sizeof(frame.bytes), frame.length);
    }

    if (!encoded)
    {
        return false;
    }

    frame.peerId = peerId;
    frame.retransmission = false;
    frame.valid = true;
    return true;
}

RadioAdapter::PollResult RadioAdapter::handleData(
    uint32_t now, size_t length, OutgoingFrame &response)
{
    Message message = {};
    uint32_t destinationId = 0;
    if (!decodeData(receiveFrame, length, message, destinationId))
    {
        return PollResult::MalformedFrame;
    }
    if (destinationId != localNodeId && destinationId != BROADCAST_ID)
    {
        return PollResult::Rejected;
    }
    if (timeReached(now, message.expiresAt))
    {
        prepareFeedback(NACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId,
                        static_cast<uint8_t>(NackReason::Expired), response);
        return PollResult::NackReady;
    }
    if (message.hopCount >= message.hopLimit)
    {
        prepareFeedback(NACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId,
                        static_cast<uint8_t>(NackReason::Invalid), response);
        return PollResult::NackReady;
    }
    if (duplicateTracker.contains(message.bundleId))
    {
        prepareFeedback(ACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId, 0, response);
        return PollResult::DuplicateData;
    }
    if (duplicateTracker.count() >= DuplicateTracker::CAPACITY)
    {
        prepareFeedback(NACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId,
                        static_cast<uint8_t>(NackReason::Busy), response);
        return PollResult::NackReady;
    }

    const Scheduler::EnqueueResult enqueueResult = scheduler.enqueue(message);
    if (enqueueResult == Scheduler::EnqueueResult::Full)
    {
        prepareFeedback(NACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId,
                        static_cast<uint8_t>(NackReason::Busy), response);
        return PollResult::NackReady;
    }
    if (enqueueResult != Scheduler::EnqueueResult::Enqueued ||
        duplicateTracker.remember(message.bundleId) !=
            DuplicateTracker::RememberResult::Added)
    {
        scheduler.removeBundle(message.bundleId);
        prepareFeedback(NACK_FRAME_TYPE, message.bundleId,
                        message.bundleId.sourceId,
                        static_cast<uint8_t>(NackReason::Invalid), response);
        return PollResult::Rejected;
    }

    prepareFeedback(ACK_FRAME_TYPE, message.bundleId,
                    message.bundleId.sourceId, 0, response);
    return PollResult::DataAccepted;
}

RadioAdapter::PollResult RadioAdapter::handleFeedback(
    size_t length, OutgoingFrame &response)
{
    FeedbackFrame feedback = {};
    if (!decodeFeedback(receiveFrame, length, feedback))
    {
        return PollResult::MalformedFrame;
    }

    if (feedback.type == ACK_FRAME_TYPE)
    {
        scheduler.acknowledge(feedback.bundleId, feedback.peerId);
    }
    else
    {
        scheduler.reject(feedback.bundleId, feedback.peerId);
    }

    clearFrame(response);
    return PollResult::FeedbackHandled;
}
}
