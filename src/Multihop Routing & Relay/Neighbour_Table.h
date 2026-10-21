#pragma once
#include "Library_Headers.h"

// Maximum number of neighbours stored simultaneously.
constexpr uint8_t MAX_NEIGHBOURS = 20;
constexpr uint32_t NEIGHBOUR_TIMEOUT_MS = 120000UL;

struct NeighbourEntry{
    uint8_t nodeID[6];       // Neighbour's MAC address
    uint32_t lastSeen;       // Local reception time in milliseconds
    int16_t RSSI;            // Latest received signal strength (dBm)
    float SNR;               // Latest signal-to-noise ratio (dB)

    int32_t latitude;        // Degrees multiplied by 10^7
    int32_t longitude;       // Degrees multiplied by 10^7
    uint16_t heading;        // Heading in degrees (0-359), matching BeaconPacket
    uint16_t speed;          // Speed in cm/s
    uint32_t timestamp;      // Sender's Unix time in seconds; 0 means unsynchronized

    float displacement;      // Distance from this node in metres

    uint16_t lastSequence;   // Last processed beacon sequence number

    uint32_t packetsReceived; // Reception statistics
    uint32_t packetsLost;     // Estimated missing packets

    float pdr;                // Estimated PDR, in percent
    bool pdrAvailable;        // False until enough data is available
};

// Initialise the neighbour table.
void initializeNeighbourTable();

// Find a neighbour using its six-byte Node ID. Returns the table index, or -1 if not found.
int findNeighbour(const uint8_t nodeID[6]);

// Create or update a neighbour from a decoded beacon.
// GPS coordinates of the current node are supplied separately.
// Returns true if the operation succeeds.
bool addOrUpdateNeighbour(
    const uint8_t nodeID[6],
    uint16_t sequence,
    uint32_t timestamp,
    int32_t latitude,
    int32_t longitude,
    uint16_t heading,
    uint16_t speed,
    int16_t rssi,
    float snr,
    int32_t currentLatitude,
    int32_t currentLongitude
);

// Calculate geographic distance between two GPS positions.
float calculateDisplacement(int32_t latitude1, int32_t longitude1, int32_t latitude2, int32_t longitude2);

// Remove neighbours that have exceeded the timeout.
void removeExpiredNeighbours();

// Recalculate displacement for every stored neighbour.
void updateAllDisplacements(int32_t currentLatitude, int32_t currentLongitude);

// Get the number of active neighbours.
uint8_t getNeighbourCount();

// Print the neighbour table to the Serial Monitor.
void printNeighbourTable();
