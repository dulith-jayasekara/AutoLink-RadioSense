#include "SCF_Codec.h"

namespace scf
{
namespace
{
void writeU32(uint8_t *output, size_t offset, uint32_t value)
{
    output[offset] = static_cast<uint8_t>(value >> 24);
    output[offset + 1] = static_cast<uint8_t>(value >> 16);
    output[offset + 2] = static_cast<uint8_t>(value >> 8);
    output[offset + 3] = static_cast<uint8_t>(value);
}

uint32_t readU32(const uint8_t *input, size_t offset)
{
    return (static_cast<uint32_t>(input[offset]) << 24) |
           (static_cast<uint32_t>(input[offset + 1]) << 16) |
           (static_cast<uint32_t>(input[offset + 2]) << 8) |
           static_cast<uint32_t>(input[offset + 3]);
}

bool validHeader(const uint8_t *input, size_t inputLength, uint8_t type,
                 uint8_t expectedLength)
{
    return input != nullptr &&
           inputLength == expectedLength &&
           inputLength >= FRAME_HEADER_SIZE &&
           input[0] == FRAME_MAGIC &&
           input[1] == MODEL_VERSION &&
           input[2] == type &&
           input[3] == expectedLength;
}

bool isValidNackReason(uint8_t reason)
{
    return reason <= static_cast<uint8_t>(NackReason::Expired);
}
}

size_t encodedDataSize(const Message &message)
{
    return isValidMessage(message)
               ? DATA_FIXED_SIZE + message.payloadLength
               : 0;
}

CodecResult encodeData(const Message &message, uint32_t destinationId,
                       uint8_t *output, size_t outputCapacity,
                       size_t &outputLength)
{
    outputLength = 0;
    const size_t frameSize = encodedDataSize(message);
    if (frameSize == 0 || destinationId == 0 || output == nullptr ||
        outputCapacity < frameSize || frameSize > UINT8_MAX)
    {
        return false;
    }

    output[0] = FRAME_MAGIC;
    output[1] = MODEL_VERSION;
    output[2] = DATA_FRAME_TYPE;
    output[3] = static_cast<uint8_t>(frameSize);
    writeU32(output, 4, message.bundleId.sourceId);
    writeU32(output, 8, destinationId);
    writeU32(output, 12, message.bundleId.sequence);
    writeU32(output, 16, message.createdAt);
    writeU32(output, 20, message.expiresAt);
    output[24] = static_cast<uint8_t>(message.priority);
    output[25] = message.hopCount;
    output[26] = message.hopLimit;
    output[27] = message.payloadLength;

    for (size_t index = 0; index < message.payloadLength; ++index)
    {
        output[DATA_FIXED_SIZE + index] = message.payload[index];
    }

    outputLength = frameSize;
    return true;
}

CodecResult decodeData(const uint8_t *input, size_t inputLength,
                       Message &message, uint32_t &destinationId)
{
    if (input == nullptr || inputLength < DATA_FIXED_SIZE ||
        inputLength > MAX_DATA_FRAME_SIZE || input[3] != inputLength ||
        input[3] < DATA_FIXED_SIZE || input[0] != FRAME_MAGIC ||
        input[1] != MODEL_VERSION || input[2] != DATA_FRAME_TYPE)
    {
        return false;
    }

    const uint8_t payloadLength = input[27];
    if (payloadLength != inputLength - DATA_FIXED_SIZE)
    {
        return false;
    }

    Message decoded = {};
    decoded.version = input[1];
    decoded.bundleId.sourceId = readU32(input, 4);
    destinationId = readU32(input, 8);
    decoded.bundleId.sequence = readU32(input, 12);
    decoded.createdAt = readU32(input, 16);
    decoded.expiresAt = readU32(input, 20);
    decoded.priority = static_cast<Priority>(input[24]);
    decoded.hopCount = input[25];
    decoded.hopLimit = input[26];
    decoded.payloadLength = payloadLength;

    for (size_t index = 0; index < payloadLength; ++index)
    {
        decoded.payload[index] = input[DATA_FIXED_SIZE + index];
    }

    if (destinationId == 0 || !isValidMessage(decoded))
    {
        return false;
    }

    message = decoded;
    return true;
}

CodecResult encodeAck(const BundleId &bundleId, uint32_t peerId,
                      uint8_t *output, size_t outputCapacity,
                      size_t &outputLength)
{
    return encodeNack(bundleId, peerId, NackReason::Unspecified,
                      output, outputCapacity, outputLength)
               ? (output[2] = ACK_FRAME_TYPE, true)
               : false;
}

CodecResult encodeNack(const BundleId &bundleId, uint32_t peerId,
                       NackReason reason, uint8_t *output,
                       size_t outputCapacity, size_t &outputLength)
{
    outputLength = 0;
    if (!isValidBundleId(bundleId) || peerId == 0 || output == nullptr ||
        outputCapacity < FEEDBACK_FRAME_SIZE)
    {
        return false;
    }

    output[0] = FRAME_MAGIC;
    output[1] = MODEL_VERSION;
    output[2] = NACK_FRAME_TYPE;
    output[3] = FEEDBACK_FRAME_SIZE;
    writeU32(output, 4, bundleId.sourceId);
    writeU32(output, 8, bundleId.sequence);
    writeU32(output, 12, peerId);
    output[16] = static_cast<uint8_t>(reason);
    outputLength = FEEDBACK_FRAME_SIZE;
    return true;
}

CodecResult decodeFeedback(const uint8_t *input, size_t inputLength,
                           FeedbackFrame &feedback)
{
    if (!validHeader(input, inputLength, ACK_FRAME_TYPE, FEEDBACK_FRAME_SIZE) &&
        !validHeader(input, inputLength, NACK_FRAME_TYPE, FEEDBACK_FRAME_SIZE))
    {
        return false;
    }

    FeedbackFrame decoded = {};
    decoded.type = input[2];
    decoded.bundleId.sourceId = readU32(input, 4);
    decoded.bundleId.sequence = readU32(input, 8);
    decoded.peerId = readU32(input, 12);
    decoded.reason = input[16];

    if (!isValidBundleId(decoded.bundleId) || decoded.peerId == 0 ||
        (decoded.type == ACK_FRAME_TYPE && decoded.reason != 0) ||
        (decoded.type == NACK_FRAME_TYPE &&
         !isValidNackReason(decoded.reason)))
    {
        return false;
    }

    feedback = decoded;
    return true;
}
}
