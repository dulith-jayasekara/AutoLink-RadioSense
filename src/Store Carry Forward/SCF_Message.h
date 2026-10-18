#pragma once

#include <stddef.h>
#include <stdint.h>

namespace scf
{
// Provisional local model. This is not the approved wire packet schema.
static constexpr uint8_t MODEL_VERSION = 1;
static constexpr size_t MAX_PAYLOAD_SIZE = 64;

enum class Priority : uint8_t
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

struct BundleId
{
    uint32_t sourceId;
    uint32_t sequence;
};

struct Message
{
    uint8_t version;
    BundleId bundleId;
    uint32_t createdAt;
    uint32_t expiresAt;
    Priority priority;
    uint8_t hopCount;
    uint8_t hopLimit;
    uint8_t payloadLength;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};

bool isValidBundleId(const BundleId &bundleId);
bool sameBundle(const BundleId &left, const BundleId &right);
bool isValidMessage(const Message &message);
bool setPayload(Message &message, const uint8_t *payload, size_t payloadLength);
}
