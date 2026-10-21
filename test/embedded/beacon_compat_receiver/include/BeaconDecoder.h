#pragma once
#include <stddef.h>
#include <stdint.h>

namespace beacon
{
constexpr size_t PACKET_SIZE = 25;
struct Packet
{
    uint8_t nodeId[6];
    uint16_t sequence;
    int32_t latitude;
    int32_t longitude;
    uint16_t speed;
    uint16_t heading;
    uint32_t timestamp;
};
enum class Result { Ok, Length, Type, Mac, Latitude, Longitude, Heading };

inline uint16_t le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline int32_t signedLe32(const uint8_t *p)
{
    const uint32_t value = le32(p);
    // Convert negative two's-complement values without out-of-range unsigned casts.
    return value <= INT32_MAX ? static_cast<int32_t>(value)
                             : -1 - static_cast<int32_t>(UINT32_MAX - value);
}
inline Result decode(const uint8_t *bytes, size_t length, Packet &output)
{
    if (!bytes || length != PACKET_SIZE) return Result::Length;
    if (bytes[0] != 0x01) return Result::Type;
    Packet packet = {};
    bool allZero = true;
    for (size_t i = 0; i < 6; ++i)
    {
        packet.nodeId[i] = bytes[1 + i];
        if (packet.nodeId[i] != 0) allZero = false;
    }
    if (allZero || (packet.nodeId[0] & 1)) return Result::Mac;
    packet.sequence = le16(bytes + 7);
    packet.latitude = signedLe32(bytes + 9);
    packet.longitude = signedLe32(bytes + 13);
    packet.speed = le16(bytes + 17);
    packet.heading = le16(bytes + 19);
    packet.timestamp = le32(bytes + 21);
    if (packet.latitude < -900000000 || packet.latitude > 900000000) return Result::Latitude;
    if (packet.longitude < -1800000000 || packet.longitude > 1800000000) return Result::Longitude;
    if (packet.heading > 359) return Result::Heading;
    // Sequence 0 is legal (including wrap). Speed accepts the full uint16 range.
    // Timestamp 0 means unsynchronized; no comparison to MCU uptime is valid.
    output = packet;
    return Result::Ok;
}
inline const char *description(Result result)
{
    switch (result)
    {
    case Result::Ok: return "valid 25-byte beacon";
    case Result::Length: return "expected exactly 25 bytes";
    case Result::Type: return "packetType must be 0x01";
    case Result::Mac: return "MAC must be nonzero unicast";
    case Result::Latitude: return "latitude outside [-90,90] degrees";
    case Result::Longitude: return "longitude outside [-180,180] degrees";
    default: return "heading outside [0,359] degrees";
    }
}
}
