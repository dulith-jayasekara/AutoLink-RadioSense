
#ifndef BEACON_H
#define BEACON_H

#include "Library_Headers.h"

// Beacon validation's GPS decision, passed to Neighbour_Table for storage.
enum class BeaconGPSUpdate : uint8_t
{
    Replace,
    MergeUnavailable,
    Preserve,
    Invalidate
};

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
    uint8_t packetType;       // 1 byte  - Beacon packet identifier (0x01)
    uint8_t nodeID[6];        // 6 bytes - Unique node ID (Wi-Fi MAC address)
    uint16_t sequenceNumber; // 2 bytes - Beacon sequence number (0-65535)
    int32_t latitude;        // 4 bytes - Latitude in degrees multiplied by 10^7
    int32_t longitude;       // 4 bytes - Longitude in degrees multiplied by 10^7
    uint16_t speed;          // 2 bytes - Speed in centimetres per second
    uint16_t heading;        // 2 bytes - Degrees (0-359), or GPS_HEADING_UNAVAILABLE
    uint32_t timestamp;      // 4 bytes - Unix timestamp in seconds
};


// Radio measurements associated with a received beacon.
struct BeaconReceptionInfo {
    int16_t rssi;
    float snr;
};

// Receiver-side GPS validation results; these fields are never serialized.
// GPS_UNAVAILABLE can mean either a missing/invalid field or an unverified
// freshness state; measurementAgeAvailable distinguishes those cases.
struct BeaconGPSFreshnessInfo
{
    // Numeric GPS_FreshnessState values, kept byte-sized to avoid a Core
    // header cycle. Beacon calculates them using the Core enum.
    uint8_t positionState;
    uint8_t speedState;
    uint8_t headingState;
    bool measurementAgeAvailable;
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

// Validate a decoded beacon before neighbour state is modified. The GPS
// update mode tells Neighbour_Table how to apply the already-validated fields.
bool validateReceivedBeacon(
    BeaconPacket &packet,
    uint16_t &sequenceDifference,
    BeaconGPSUpdate &gpsUpdate,
    BeaconGPSFreshnessInfo &freshnessInfo
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
