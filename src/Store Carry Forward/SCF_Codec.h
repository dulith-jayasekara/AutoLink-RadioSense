#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SCF_Message.h"

namespace scf
{
static constexpr uint8_t FRAME_MAGIC = 0xA5;
static constexpr uint8_t FRAME_HEADER_SIZE = 4;
static constexpr uint8_t DATA_FRAME_TYPE = 1;
static constexpr uint8_t ACK_FRAME_TYPE = 2;
static constexpr uint8_t NACK_FRAME_TYPE = 3;
static constexpr size_t DATA_FIXED_SIZE = 28;
static constexpr size_t FEEDBACK_FRAME_SIZE = 17;
static constexpr size_t MAX_DATA_FRAME_SIZE = DATA_FIXED_SIZE + MAX_PAYLOAD_SIZE;

using CodecResult = bool;

enum class NackReason : uint8_t
{
    Unspecified = 0,
    Busy = 1,
    Invalid = 2,
    Unsupported = 3,
    Expired = 4
};

struct FeedbackFrame
{
    uint8_t type;
    BundleId bundleId;
    uint32_t peerId;
    uint8_t reason;
};

size_t encodedDataSize(const Message &message);
CodecResult encodeData(const Message &message, uint32_t destinationId,
                       uint8_t *output, size_t outputCapacity,
                       size_t &outputLength);
CodecResult decodeData(const uint8_t *input, size_t inputLength,
                       Message &message, uint32_t &destinationId);
CodecResult encodeAck(const BundleId &bundleId, uint32_t peerId,
                      uint8_t *output, size_t outputCapacity,
                      size_t &outputLength);
CodecResult encodeNack(const BundleId &bundleId, uint32_t peerId,
                       NackReason reason, uint8_t *output,
                       size_t outputCapacity, size_t &outputLength);
CodecResult decodeFeedback(const uint8_t *input, size_t inputLength,
                           FeedbackFrame &feedback);
}
