#include "../../../src/Store Carry Forward/SCF_Codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace
{
scf::Message makeMessage()
{
    scf::Message message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {0x01020304U, 0xA0B0C0D0U};
    message.createdAt = 0x10203040U;
    message.expiresAt = 0x50607080U;
    message.priority = scf::Priority::Critical;
    message.hopCount = 2;
    message.hopLimit = 7;
    const uint8_t payload[] = {0x00, 0x7F, 0x80, 0xFF};
    assert(scf::setPayload(message, payload, sizeof(payload)));
    return message;
}

void testDataRoundTripAndByteOrder()
{
    const scf::Message original = makeMessage();
    uint8_t frame[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t frameLength = 0;
    assert(scf::encodeData(original, 0x11223344U, frame, sizeof(frame), frameLength));
    assert(frameLength == scf::DATA_FIXED_SIZE + 4);
    assert(frame[0] == scf::FRAME_MAGIC);
    assert(frame[1] == scf::MODEL_VERSION);
    assert(frame[2] == scf::DATA_FRAME_TYPE);
    assert(frame[3] == frameLength);
    assert(frame[4] == 0x01 && frame[5] == 0x02 && frame[6] == 0x03 && frame[7] == 0x04);
    assert(frame[8] == 0x11 && frame[9] == 0x22 && frame[10] == 0x33 && frame[11] == 0x44);

    scf::Message decoded = {};
    uint32_t destinationId = 0;
    assert(scf::decodeData(frame, frameLength, decoded, destinationId));
    assert(destinationId == 0x11223344U);
    assert(decoded.version == original.version);
    assert(scf::sameBundle(decoded.bundleId, original.bundleId));
    assert(decoded.createdAt == original.createdAt);
    assert(decoded.expiresAt == original.expiresAt);
    assert(decoded.priority == original.priority);
    assert(decoded.hopCount == original.hopCount);
    assert(decoded.hopLimit == original.hopLimit);
    assert(decoded.payloadLength == original.payloadLength);
    for (size_t index = 0; index < original.payloadLength; ++index)
    {
        assert(decoded.payload[index] == original.payload[index]);
    }
}

void testDataBoundariesAndMalformedFrames()
{
    scf::Message message = makeMessage();
    uint8_t frame[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t frameLength = 0;
    assert(scf::setPayload(message, message.payload, scf::MAX_PAYLOAD_SIZE));
    assert(scf::encodeData(message, 1, frame, sizeof(frame), frameLength));
    assert(frameLength == scf::MAX_DATA_FRAME_SIZE);

    scf::Message decoded = {};
    uint32_t destinationId = 0;
    assert(!scf::decodeData(frame, frameLength - 1, decoded, destinationId));
    frame[3] = static_cast<uint8_t>(frameLength - 1);
    assert(!scf::decodeData(frame, frameLength, decoded, destinationId));
    frame[3] = static_cast<uint8_t>(frameLength);
    frame[1] = 99;
    assert(!scf::decodeData(frame, frameLength, decoded, destinationId));
    frame[1] = scf::MODEL_VERSION;
    frame[27] = 0;
    assert(!scf::decodeData(frame, frameLength, decoded, destinationId));
    assert(!scf::encodeData(message, 0, frame, sizeof(frame), frameLength));
}

void testFeedbackRoundTripsAndMalformedFrames()
{
    const scf::BundleId bundleId = {0x01020304U, 0x05060708U};
    uint8_t frame[scf::FEEDBACK_FRAME_SIZE] = {};
    size_t frameLength = 0;
    assert(scf::encodeAck(bundleId, 0xAABBCCDDU, frame, sizeof(frame), frameLength));
    assert(frameLength == scf::FEEDBACK_FRAME_SIZE);

    scf::FeedbackFrame feedback = {};
    assert(scf::decodeFeedback(frame, frameLength, feedback));
    assert(feedback.type == scf::ACK_FRAME_TYPE);
    assert(scf::sameBundle(feedback.bundleId, bundleId));
    assert(feedback.peerId == 0xAABBCCDDU);
    assert(feedback.reason == 0);

    assert(scf::encodeNack(bundleId, 9, scf::NackReason::Busy,
                          frame, sizeof(frame), frameLength));
    assert(scf::decodeFeedback(frame, frameLength, feedback));
    assert(feedback.type == scf::NACK_FRAME_TYPE);
    assert(feedback.reason == static_cast<uint8_t>(scf::NackReason::Busy));

    frame[16] = 99;
    assert(!scf::decodeFeedback(frame, frameLength, feedback));
    frame[0] = 0;
    assert(!scf::decodeFeedback(frame, frameLength, feedback));
    assert(!scf::decodeFeedback(frame, frameLength - 1, feedback));
    assert(!scf::encodeAck({0, 1}, 9, frame, sizeof(frame), frameLength));
    assert(!scf::encodeNack(bundleId, 0, scf::NackReason::Busy,
                            frame, sizeof(frame), frameLength));
}
}

int main()
{
    testDataRoundTripAndByteOrder();
    testDataBoundariesAndMalformedFrames();
    testFeedbackRoundTripsAndMalformedFrames();

    std::cout << "SCF codec unit tests passed.\n";
    return 0;
}
