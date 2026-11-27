
#ifndef BEACON_H
#define BEACON_H

#include "Library_Headers.h"
#include "Project_Headers.h"
#include "Config.h"

// ---------------------------------------------------------
// BEACON PACKET CONFIGURATION
// ---------------------------------------------------------

constexpr uint8_t BEACON_PACKET_TYPE = 0x01;
constexpr uint8_t BEACON_MAC_SIZE = 6;
constexpr uint8_t BEACON_PACKET_SIZE = 25;


// ---------------------------------------------------------
// ADAPTIVE BEACON TRANSMISSION CONFIGURATION
// ---------------------------------------------------------

// Maximum interval used when the vehicle is stationary.
constexpr uint32_t BEACON_MAX_INTERVAL_MS = 15000;

// Minimum interval used when the vehicle reaches or exceeds the reference maximum speed.
constexpr uint32_t BEACON_MIN_INTERVAL_MS = 2000;

// Default interval used when vehicle speed is unavailable.
constexpr uint32_t BEACON_DEFAULT_INTERVAL_MS = 5000;

// Reference speed at which the minimum interval is reached.
constexpr float BEACON_MAX_SPEED_KMPH = 100.0f;

// Speeds below or equal to this value are considered stationary.
constexpr float BEACON_STATIONARY_SPEED_KMPH = 1.8f;


// ---------------------------------------------------------
// BEACON PACKET STRUCTURE
// ---------------------------------------------------------

struct BeaconPacket {
    uint8_t packetType;
    uint8_t nodeID[6];
    uint16_t sequenceNumber;
    int32_t latitude;
    int32_t longitude;
    uint16_t speed;       // Centimetres per second
    uint16_t heading;     // Degrees (0-359), or GPS_HEADING_UNAVAILABLE
    uint32_t timestamp;   // Unix time in seconds
};


// Radio measurements associated with a received beacon.
struct BeaconReceptionInfo {
    int16_t rssi;
    float snr;
};


// ---------------------------------------------------------
// FUNCTION DECLARATIONS
// ---------------------------------------------------------

uint16_t generateSequenceNumber();

void generateBeacon(BeaconPacket &packet);

bool transmitBeacon(BeaconPacket &packet);

bool receiveBeacon(
    BeaconPacket &packet,
    BeaconReceptionInfo &receptionInfo
);

void extractBeaconInformation(const BeaconPacket &packet);

// Calculate the adaptive beacon transmission interval.
uint32_t calculateBeaconInterval(
    uint16_t speed,
    bool speedAvailable
);

// Manage periodic adaptive beacon transmission.
void handleBeaconTransmission(
    uint16_t speed,
    bool speedAvailable
);

#endif
