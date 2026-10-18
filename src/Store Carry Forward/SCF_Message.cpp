#include "SCF_Message.h"

namespace scf
{
namespace
{
bool isValidPriority(Priority priority)
{
    return priority == Priority::Low ||
           priority == Priority::Normal ||
           priority == Priority::High ||
           priority == Priority::Critical;
}
}

bool isValidBundleId(const BundleId &bundleId)
{
    return bundleId.sourceId != 0 && bundleId.sequence != 0;
}

bool sameBundle(const BundleId &left, const BundleId &right)
{
    return left.sourceId == right.sourceId &&
           left.sequence == right.sequence;
}

bool isValidMessage(const Message &message)
{
    return message.version == MODEL_VERSION &&
           isValidBundleId(message.bundleId) &&
           message.createdAt < message.expiresAt &&
           isValidPriority(message.priority) &&
           message.hopCount <= message.hopLimit &&
           message.payloadLength <= MAX_PAYLOAD_SIZE;
}

bool setPayload(Message &message, const uint8_t *payload, size_t payloadLength)
{
    if (payloadLength > MAX_PAYLOAD_SIZE ||
        (payloadLength > 0 && payload == nullptr))
    {
        return false;
    }

    for (size_t index = 0; index < payloadLength; ++index)
    {
        message.payload[index] = payload[index];
    }

    message.payloadLength = static_cast<uint8_t>(payloadLength);
    return true;
}
}
