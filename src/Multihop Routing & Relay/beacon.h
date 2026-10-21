#ifndef BEACON_H
#define BEACON_H

#include "Library_Headers.h"

// Beacon packet configuration
constexpr uint8_t BEACON_PACKET_TYPE = 0x01;
constexpr uint8_t BEACON_MAC_SIZE = 6;
constexpr uint8_t BEACON_PACKET_SIZE = 25;
constexpr uint32_t BEACON_INTERVAL_MS = 5000;

// Beacon packet structure
struct BeaconPacket {
    uint8_t packetType;          // 1 byte
    uint8_t nodeID[6];           // Wi-Fi MAC address: 6 bytes
    uint16_t sequenceNumber;     // 2 bytes
    int32_t latitude;            // 4 bytes: degrees * 10^7
    int32_t longitude;           // 4 bytes: degrees * 10^7
    uint16_t speed;              // 2 bytes: centimetres per second
    uint16_t heading;            // 2 bytes: degrees (0-359)
    uint32_t timestamp;          // 4 bytes: Unix time in seconds
};

uint16_t generateSequenceNumber();
void generateBeacon(BeaconPacket &packet);
bool transmitBeacon(BeaconPacket &packet);
bool receiveBeacon(BeaconPacket &packet);
void extractBeaconInformation(const BeaconPacket &packet);

#endif
