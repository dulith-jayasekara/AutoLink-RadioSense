#include "../../../src/Store Carry Forward/SCF_Radio_Adapter.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
class FakeRadio final : public scf::RadioPort
{
public:
    bool initializeResult = true;
    bool transmitResult = true;
    bool packetReady = false;
    bool overflow = false;
    bool receiveError = false;
    uint8_t incoming[scf::RADIO_FRAME_CAPACITY] = {};
    size_t incomingLength = 0;
    size_t transmittedCount = 0;
    uint8_t lastTransmitted[scf::RADIO_FRAME_CAPACITY] = {};
    size_t lastTransmittedLength = 0;

    bool initialize() override
    {
        return initializeResult;
    }

    scf::PortReceiveResult receive(uint8_t *frame, size_t capacity,
                                   size_t &length) override
    {
        length = 0;
        if (receiveError)
        {
            receiveError = false;
            return scf::PortReceiveResult::Error;
        }
        if (!packetReady)
        {
            return scf::PortReceiveResult::NoPacket;
        }
        packetReady = false;
        if (overflow || incomingLength > capacity)
        {
            overflow = false;
            return scf::PortReceiveResult::Overflow;
        }
        for (size_t index = 0; index < incomingLength; ++index)
        {
            frame[index] = incoming[index];
        }
        length = incomingLength;
        return scf::PortReceiveResult::Packet;
    }

    bool transmit(const uint8_t *frame, size_t length) override
    {
        if (!transmitResult)
        {
            return false;
        }
        ++transmittedCount;
        lastTransmittedLength = length;
        for (size_t index = 0; index < length; ++index)
        {
            lastTransmitted[index] = frame[index];
        }
        return true;
    }

    void queue(const uint8_t *frame, size_t length)
    {
        incomingLength = length;
        for (size_t index = 0; index < length; ++index)
        {
            incoming[index] = frame[index];
        }
        packetReady = true;
    }
};

scf::Message makeMessage(uint32_t sourceId = 1, uint32_t sequence = 1,
                         uint32_t expiresAt = 1000,
                         uint8_t hopCount = 0,
                         uint8_t hopLimit = 4)
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {sourceId, sequence};
    message.createdAt = 0;
    message.expiresAt = expiresAt;
    message.priority = scf::Priority::High;
    message.hopCount = hopCount;
    message.hopLimit = hopLimit;
    return message;
}

void testInitializationAndMalformedReceive()
{
    FakeRadio radio;
    scf::RadioAdapter adapter(radio, 9);
    scf::RadioAdapter::OutgoingFrame response = {};
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::NotInitialized);
    assert(adapter.initialize() == scf::RadioAdapter::InitResult::Initialized);
    assert(adapter.initialize() == scf::RadioAdapter::InitResult::AlreadyInitialized);

    const uint8_t malformed[] = {scf::FRAME_MAGIC, scf::MODEL_VERSION,
                                 scf::DATA_FRAME_TYPE, 4};
    radio.queue(malformed, sizeof(malformed));
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::MalformedFrame);
    assert(!response.valid);
}

void testDataAdmissionAckAndDuplicate()
{
    FakeRadio radio;
    scf::RadioAdapter adapter(radio, 9);
    assert(adapter.initialize() == scf::RadioAdapter::InitResult::Initialized);

    const scf::Message message = makeMessage(3, 7);
    uint8_t data[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t dataLength = 0;
    assert(scf::encodeData(message, 9, data, sizeof(data), dataLength));
    radio.queue(data, dataLength);

    scf::RadioAdapter::OutgoingFrame response = {};
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::DataAccepted);
    assert(response.valid);
    assert(response.bytes[2] == scf::ACK_FRAME_TYPE);
    assert(adapter.bundleCount() == 1);
    assert(adapter.transmit(response, 10) == scf::RadioAdapter::TransmitResult::Sent);

    radio.queue(data, dataLength);
    assert(adapter.poll(11, response) == scf::RadioAdapter::PollResult::DuplicateData);
    assert(response.valid);
    assert(response.bytes[2] == scf::ACK_FRAME_TYPE);
}

void testExpiryHopLimitAndOverflow()
{
    FakeRadio radio;
    scf::RadioAdapter adapter(radio, 9);
    adapter.initialize();
    scf::RadioAdapter::OutgoingFrame response = {};
    uint8_t data[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t dataLength = 0;

    assert(scf::encodeData(makeMessage(3, 1, 10), 9, data, sizeof(data), dataLength));
    radio.queue(data, dataLength);
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::NackReady);
    assert(response.bytes[2] == scf::NACK_FRAME_TYPE);

    assert(scf::encodeData(makeMessage(3, 2, 100, 4, 4), 9,
                           data, sizeof(data), dataLength));
    radio.queue(data, dataLength);
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::NackReady);

    radio.packetReady = true;
    radio.overflow = true;
    assert(adapter.poll(10, response) == scf::RadioAdapter::PollResult::Overflow);
    assert(adapter.bundleCount() == 0);
}

void testFeedbackForwardingRetryAndRadioFailure()
{
    FakeRadio radio;
    scf::RadioAdapter adapter(radio, 9, 1, 100);
    adapter.initialize();
    const scf::Message message = makeMessage(9, 1);
    assert(adapter.enqueue(message) == scf::Scheduler::EnqueueResult::Enqueued);

    scf::RadioAdapter::OutgoingFrame outgoing = {};
    assert(adapter.prepareEncounter(7, 10, outgoing) ==
           scf::RadioAdapter::PrepareResult::Ready);
    assert(!outgoing.retransmission);
    assert(adapter.transmit(outgoing, 10) == scf::RadioAdapter::TransmitResult::Sent);

    uint8_t nack[scf::FEEDBACK_FRAME_SIZE] = {};
    size_t nackLength = 0;
    assert(scf::encodeNack(message.bundleId, 7, scf::NackReason::Busy,
                           nack, sizeof(nack), nackLength));
    radio.queue(nack, nackLength);
    assert(adapter.poll(11, outgoing) == scf::RadioAdapter::PollResult::FeedbackHandled);
    assert(adapter.prepareEncounter(7, 12, outgoing) ==
           scf::RadioAdapter::PrepareResult::Ready);
    assert(outgoing.retransmission);
    assert(adapter.transmit(outgoing, 12) == scf::RadioAdapter::TransmitResult::Sent);

    radio.transmitResult = false;
    assert(adapter.prepareEncounter(8, 20, outgoing) ==
           scf::RadioAdapter::PrepareResult::Ready);
    assert(adapter.transmit(outgoing, 20) == scf::RadioAdapter::TransmitResult::RadioFailure);
}

void testTimeoutRetryAndDuplicateAckMatching()
{
    FakeRadio radio;
    scf::RadioAdapter adapter(radio, 9, 1, 100);
    adapter.initialize();
    const scf::Message message = makeMessage(9, 2);
    adapter.enqueue(message);
    scf::RadioAdapter::OutgoingFrame frame = {};
    assert(adapter.prepareEncounter(7, 10, frame) ==
           scf::RadioAdapter::PrepareResult::Ready);
    assert(adapter.transmit(frame, 10) == scf::RadioAdapter::TransmitResult::Sent);

    assert(adapter.tick(109, frame) == scf::RadioAdapter::TickResult::Waiting);
    assert(adapter.tick(110, frame) == scf::RadioAdapter::TickResult::RetryReady);
    assert(frame.retransmission);
    assert(adapter.transmit(frame, 110) == scf::RadioAdapter::TransmitResult::Sent);
    assert(adapter.tick(210, frame) == scf::RadioAdapter::TickResult::Failed);
    assert(!frame.valid);
}
}

int main()
{
    testInitializationAndMalformedReceive();
    testDataAdmissionAckAndDuplicate();
    testExpiryHopLimitAndOverflow();
    testFeedbackForwardingRetryAndRadioFailure();
    testTimeoutRetryAndDuplicateAckMatching();

    std::cout << "SCF radio adapter unit tests passed.\n";
    return 0;
}
