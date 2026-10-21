#include "Project_Headers.h"
#include "Library_Headers.h"

// Private neighbour table.
static NeighbourEntry neighbourTable[MAX_NEIGHBOURS];

// Number of active entries.
static uint8_t neighbourCount = 0;

// Track whether a table slot is occupied.
static bool occupied[MAX_NEIGHBOURS] = {false};

// --------------------------------------------------
// Initialise the table
// --------------------------------------------------
void initializeNeighbourTable()
{
    memset(neighbourTable, 0, sizeof(neighbourTable));
    memset(occupied, 0, sizeof(occupied));

    neighbourCount = 0;
}

// --------------------------------------------------
// Find a neighbour by its six-byte Node ID
// --------------------------------------------------
int findNeighbour(const uint8_t nodeID[6])
{
    if (nodeID == nullptr)
        return -1;

    for (uint8_t i = 0; i < MAX_NEIGHBOURS; i++)
    {
        if (occupied[i] &&
            memcmp(neighbourTable[i].nodeID, nodeID, 6) == 0)
        {
            return i;
        }
    }

    return -1;
}

// --------------------------------------------------
// Calculate distance using the Haversine formula
// Coordinates are scaled by 10^7.
// Returns distance in metres.
// --------------------------------------------------
float calculateDisplacement(
    int32_t latitude1,
    int32_t longitude1,
    int32_t latitude2,
    int32_t longitude2)
{
    constexpr int32_t MAX_LATITUDE = 900000000;
    constexpr int32_t MAX_LONGITUDE = 1800000000;
    if (latitude1 < -MAX_LATITUDE || latitude1 > MAX_LATITUDE ||
        latitude2 < -MAX_LATITUDE || latitude2 > MAX_LATITUDE ||
        longitude1 < -MAX_LONGITUDE || longitude1 > MAX_LONGITUDE ||
        longitude2 < -MAX_LONGITUDE || longitude2 > MAX_LONGITUDE)
    {
        return NAN;
    }

    constexpr double EARTH_RADIUS = 6371000.0;
    constexpr double SCALE = 10000000.0;
    constexpr double RADIANS_PER_DEGREE =
        0.017453292519943295769236907684886;

    double lat1 = (latitude1 / SCALE) * RADIANS_PER_DEGREE;
    double lon1 = (longitude1 / SCALE) * RADIANS_PER_DEGREE;

    double lat2 = (latitude2 / SCALE) * RADIANS_PER_DEGREE;
    double lon2 = (longitude2 / SCALE) * RADIANS_PER_DEGREE;

    double deltaLat = lat2 - lat1;
    double deltaLon = lon2 - lon1;

    double sinLat = sin(deltaLat / 2.0);
    double sinLon = sin(deltaLon / 2.0);

    double a = sinLat * sinLat +
               cos(lat1) * cos(lat2) * sinLon * sinLon;

    // Avoid numerical errors outside the valid range.
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    return static_cast<float>(EARTH_RADIUS * c);
}

// --------------------------------------------------
// Create or update a neighbour
// --------------------------------------------------
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
    int32_t currentLongitude)
{
    if (nodeID == nullptr)
        return false;

    // Do not add this device as its own neighbour.
    uint8_t ownID[6];
    WiFi.macAddress(ownID);

    if (memcmp(nodeID, ownID, 6) == 0)
        return false;

    int index = findNeighbour(nodeID);
    bool isNew = (index == -1);

    // Find a free slot for a new neighbour.
    if (isNew)
    {
        for (uint8_t i = 0; i < MAX_NEIGHBOURS; i++)
        {
            if (!occupied[i])
            {
                index = i;
                break;
            }
        }

        // Table is full.
        if (index == -1)
        {
            Serial.println("Neighbour table full; rejecting new neighbour.");
            return false;
        }

        NeighbourEntry &entry = neighbourTable[index];

        memset(&entry, 0, sizeof(entry));
        memcpy(entry.nodeID, nodeID, 6);

        entry.lastSequence = sequence;
        entry.packetsReceived = 1;
        entry.packetsLost = 0;
        entry.pdr = 0.0f;
        entry.pdrAvailable = false;

        occupied[index] = true;
        neighbourCount++;
    }
    else
    {
        NeighbourEntry &entry = neighbourTable[index];

        // Sequence-number difference uses 16-bit modular arithmetic.
        uint16_t difference =
            static_cast<uint16_t>(sequence - entry.lastSequence);

        // Only accept forward progression within half the sequence space.
        // Duplicate or older packets do not change sequence statistics.
        if (difference == 0 || difference >= 32768)
            return false;

        entry.packetsReceived++;

        if (difference > 1)
        {
            entry.packetsLost += difference - 1;
        }

        entry.lastSequence = sequence;

        uint32_t expected =
            entry.packetsReceived + entry.packetsLost;

        if (expected > 0)
        {
            entry.pdr =
                (100.0f * entry.packetsReceived) / expected;

            entry.pdrAvailable = true;
        }
    }

    // Update the selected entry.
    NeighbourEntry &entry = neighbourTable[index];

    memcpy(entry.nodeID, nodeID, 6);

    entry.lastSeen = millis();

    entry.RSSI = rssi;
    entry.SNR = snr;

    entry.latitude = latitude;
    entry.longitude = longitude;
    entry.timestamp = timestamp;

    entry.heading = heading;
    entry.speed = speed;

    entry.displacement = calculateDisplacement(
        currentLatitude,
        currentLongitude,
        latitude,
        longitude
    );

    return true;
}

// --------------------------------------------------
// Recalculate distances after our GPS position changes
// --------------------------------------------------
void updateAllDisplacements(
    int32_t currentLatitude,
    int32_t currentLongitude)
{
    for (uint8_t i = 0; i < MAX_NEIGHBOURS; i++)
    {
        if (!occupied[i])
            continue;

        neighbourTable[i].displacement = calculateDisplacement(
            currentLatitude,
            currentLongitude,
            neighbourTable[i].latitude,
            neighbourTable[i].longitude
        );
    }
}

// --------------------------------------------------
// Remove expired neighbours
// --------------------------------------------------
void removeExpiredNeighbours()
{
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_NEIGHBOURS; i++)
    {
        if (!occupied[i])
            continue;

        uint32_t elapsed = now - neighbourTable[i].lastSeen;

        if (elapsed >= NEIGHBOUR_TIMEOUT_MS)
        {
            occupied[i] = false;
            memset(&neighbourTable[i], 0,
                   sizeof(NeighbourEntry));

            if (neighbourCount > 0)
                neighbourCount--;
        }
    }
}

// --------------------------------------------------
// Return active neighbour count
// --------------------------------------------------
uint8_t getNeighbourCount()
{
    return neighbourCount;
}

// --------------------------------------------------
// Print the neighbour table
// --------------------------------------------------
void printNeighbourTable()
{
    Serial.println("\n===== NEIGHBOUR TABLE =====");

    Serial.print("Active neighbours: ");
    Serial.println(neighbourCount);

    for (uint8_t i = 0; i < MAX_NEIGHBOURS; i++)
    {
        if (!occupied[i])
            continue;

        const NeighbourEntry &entry = neighbourTable[i];

        Serial.printf(
            "Node ID: %02X:%02X:%02X:%02X:%02X:%02X\n",
            entry.nodeID[0], entry.nodeID[1],
            entry.nodeID[2], entry.nodeID[3],
            entry.nodeID[4], entry.nodeID[5]
        );

        Serial.printf("Last seen: %lu ms ago\n",
            (unsigned long)(millis() - entry.lastSeen));

        Serial.printf("RSSI: %d dBm\n", entry.RSSI);
        Serial.printf("SNR: %.2f dB\n", entry.SNR);

        Serial.printf("Latitude: %.7f\n",
            entry.latitude / 10000000.0);

        Serial.printf("Longitude: %.7f\n",
            entry.longitude / 10000000.0);

        Serial.printf("Heading: %u degrees\n",
            static_cast<unsigned int>(entry.heading));

        Serial.printf("Speed: %.2f m/s\n",
            entry.speed / 100.0f);

        if (entry.timestamp == 0)
        {
            Serial.println("Beacon timestamp: not synchronized (0)");
        }
        else
        {
            Serial.printf("Beacon timestamp: %lu Unix seconds\n",
                static_cast<unsigned long>(entry.timestamp));
        }

        Serial.printf("Displacement: %.2f metres\n",
            entry.displacement);

        if (entry.pdrAvailable)
        {
            Serial.printf("PDR: %.2f%%\n", entry.pdr);
        }
        else
        {
            Serial.println("PDR: Not available");
        }

        Serial.println("---------------------------");
    }
}
