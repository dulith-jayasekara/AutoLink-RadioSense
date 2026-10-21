#include "BeaconDecoder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
int main()
{
    // Literal bytes independently exercise offsets, endian order and signed values:
    // lat=-1 deg, lon=2 deg, speed=1234 cm/s, heading=359, timestamp=0x12345678.
    const uint8_t golden[25] = {1, 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC,
        0x34, 0x12, 0x80, 0x69, 0x67, 0xFF, 0x00, 0x2D, 0x31, 0x01,
        0xD2, 0x04, 0x67, 0x01, 0x78, 0x56, 0x34, 0x12};
    beacon::Packet p = {};
    assert(beacon::decode(golden, 25, p) == beacon::Result::Ok);
    assert(memcmp(p.nodeId, golden + 1, 6) == 0 && p.sequence == 0x1234);
    assert(p.latitude == -10000000 && p.longitude == 20000000);
    assert(p.speed == 1234 && p.heading == 359 && p.timestamp == 0x12345678);
    for (size_t n = 0; n < 25; ++n) assert(beacon::decode(golden, n, p) == beacon::Result::Length);
    assert(beacon::decode(golden, 26, p) == beacon::Result::Length);
    assert(beacon::decode(nullptr, 25, p) == beacon::Result::Length);
    uint8_t bytes[25];
    memcpy(bytes, golden, 25);
    bytes[0] = 2;
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Type);
    memcpy(bytes, golden, 25);
    memset(bytes + 1, 0, 6);
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Mac);
    memcpy(bytes, golden, 25);
    bytes[1] |= 1;
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Mac);
    memcpy(bytes, golden, 25);
    put32(bytes + 21, 0);
    bytes[7] = bytes[8] = 0;
    bytes[17] = bytes[18] = 255;
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Ok && p.timestamp == 0 && p.speed == 65535);
    const int signs[] = {-1, 1};
    for (int sign : signs)
    {
        put32(bytes + 9, static_cast<uint32_t>(sign * 900000000));
        put32(bytes + 13, static_cast<uint32_t>(sign * 1800000000));
        assert(beacon::decode(bytes, 25, p) == beacon::Result::Ok);
        put32(bytes + 9, static_cast<uint32_t>(sign * 900000001));
        assert(beacon::decode(bytes, 25, p) == beacon::Result::Latitude);
        put32(bytes + 9, 0);
        put32(bytes + 13, static_cast<uint32_t>(sign * 1800000001));
        assert(beacon::decode(bytes, 25, p) == beacon::Result::Longitude);
    }
    put32(bytes + 13, 0);
    bytes[19] = 0x68; bytes[20] = 1;
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Heading);
    bytes[19] = bytes[20] = 0;
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Ok);
    put32(bytes + 9, 0x80000000U);
    assert(beacon::decode(bytes, 25, p) == beacon::Result::Latitude);
    puts("PASS: beacon decoder golden packet, endian/sign, lengths, fields and boundaries");
}
