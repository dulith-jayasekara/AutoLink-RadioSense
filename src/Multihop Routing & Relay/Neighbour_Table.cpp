#include "Neighbour_Table.h"
#include "beacon.h"

// Private neighbour table.
static NeighbourEntry neighbourTable[MAX_NEIGHBOURS];

// Number of active entries.
static uint8_t neighbourCount = 0;

// Track whether a table slot is occupied.
static bool occupied[MAX_NEIGHBOURS] = {false};

namespace
{
    bool HasPosition(int32_t latitude, int32_t longitude)
    {
        return latitude != GPS_LATITUDE_UNAVAILABLE && longitude != GPS_LONGITUDE_UNAVAILABLE;
    }

    void InvalidateNeighbourGPS(NeighbourEntry &entry)
    {
        entry.latitude = GPS_LATITUDE_UNAVAILABLE;
        entry.longitude = GPS_LONGITUDE_UNAVAILABLE;
        entry.speed = GPS_SPEED_UNAVAILABLE;
        entry.heading = GPS_HEADING_UNAVAILABLE;
        entry.timestamp = GPS_TIMESTAMP_UNAVAILABLE;
        entry.displacement = NAN;
    }

    void UpdateNeighbourGPS(
        NeighbourEntry &entry,
        BeaconGPSUpdate updateMode,
        uint32_t timestamp,
        int32_t latitude,
        int32_t longitude,
        uint16_t heading,
        uint16_t speed,
        int32_t currentLatitude,
        int32_t currentLongitude)
    {
        switch (updateMode)
        {
        case BeaconGPSUpdate::Invalidate:
            InvalidateNeighbourGPS(entry);
            return;
        case BeaconGPSUpdate::Preserve:
            return;
        case BeaconGPSUpdate::Replace:
            entry.lastGpsMeasurementTimestamp = timestamp;
            entry.timestamp = timestamp;
            entry.latitude = latitude;
            entry.longitude = longitude;
            entry.heading = heading;
            entry.speed = speed;
            break;
        case BeaconGPSUpdate::MergeUnavailable:
            // Same-timestamp beacons cannot refresh GPS values, but may
            // report that one or more previously supplied fields expired.
            if (latitude == GPS_LATITUDE_UNAVAILABLE ||
                longitude == GPS_LONGITUDE_UNAVAILABLE)
            {
                entry.latitude = GPS_LATITUDE_UNAVAILABLE;
                entry.longitude = GPS_LONGITUDE_UNAVAILABLE;
            }
            if (heading == GPS_HEADING_UNAVAILABLE)
            {
                entry.heading = GPS_HEADING_UNAVAILABLE;
            }
            if (speed == GPS_SPEED_UNAVAILABLE)
            {
                entry.speed = GPS_SPEED_UNAVAILABLE;
            }
            break;
        default:
            return;
        }

        if (!HasPosition(entry.latitude, entry.longitude) ||
            !HasPosition(currentLatitude, currentLongitude))
        {
            entry.displacement = NAN;
        }
        else
        {
            entry.displacement = calculateDisplacement(
                currentLatitude,
                currentLongitude,
                entry.latitude,
                entry.longitude
            );
        }

        if (entry.latitude == GPS_LATITUDE_UNAVAILABLE &&
            entry.longitude == GPS_LONGITUDE_UNAVAILABLE &&
            entry.speed == GPS_SPEED_UNAVAILABLE &&
            entry.heading == GPS_HEADING_UNAVAILABLE)
        {
            entry.timestamp = GPS_TIMESTAMP_UNAVAILABLE;
        }
    }
}

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

bool getNeighbourValidationHistory(
    const uint8_t nodeID[6],
    uint16_t &lastSequence,
    uint32_t &lastGpsMeasurementTimestamp)
{
    lastSequence = 0;
    lastGpsMeasurementTimestamp = GPS_TIMESTAMP_UNAVAILABLE;

    const int index = findNeighbour(nodeID);
    if (index < 0)
    {
        return false;
    }

    const NeighbourEntry &entry = neighbourTable[index];
    lastSequence = entry.lastSequence;
    lastGpsMeasurementTimestamp = entry.lastGpsMeasurementTimestamp;
    return true;
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
    if (latitude1 == GPS_LATITUDE_UNAVAILABLE ||
        longitude1 == GPS_LONGITUDE_UNAVAILABLE ||
        latitude2 == GPS_LATITUDE_UNAVAILABLE ||
        longitude2 == GPS_LONGITUDE_UNAVAILABLE)
    {
        return NAN;
    }

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
    uint16_t sequenceDifference,
    BeaconGPSUpdate gpsUpdate,
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
        InvalidateNeighbourGPS(entry);
        entry.lastGpsMeasurementTimestamp = GPS_TIMESTAMP_UNAVAILABLE;

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
        entry.packetsReceived++;

        if (sequenceDifference > 1)
        {
            entry.packetsLost += sequenceDifference - 1;
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

    UpdateNeighbourGPS(
        entry,
        gpsUpdate,
        timestamp,
        latitude,
        longitude,
        heading,
        speed,
        currentLatitude,
        currentLongitude);

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

        if (!HasPosition(
                neighbourTable[i].latitude,
                neighbourTable[i].longitude) ||
            !HasPosition(currentLatitude, currentLongitude))
        {
            neighbourTable[i].displacement = NAN;
        }
        else
        {
            neighbourTable[i].displacement = calculateDisplacement(
                currentLatitude,
                currentLongitude,
                neighbourTable[i].latitude,
                neighbourTable[i].longitude
            );
        }
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

        if (HasPosition(entry.latitude, entry.longitude))
        {
            Serial.printf("Latitude: %.7f\n",
                entry.latitude / 10000000.0);
            Serial.printf("Longitude: %.7f\n",
                entry.longitude / 10000000.0);
        }
        else
        {
            Serial.println("Position: unavailable");
        }

        if (entry.heading == GPS_HEADING_UNAVAILABLE)
        {
            Serial.println("Heading: unavailable");
        }
        else
        {
            Serial.printf("Heading: %u degrees\n",
                static_cast<unsigned int>(entry.heading));
        }

        if (entry.speed == GPS_SPEED_UNAVAILABLE)
            Serial.println("Speed: unavailable");
        else
            Serial.printf("Speed: %.2f m/s\n", entry.speed / 100.0f);

        if (entry.timestamp == GPS_TIMESTAMP_UNAVAILABLE)
        {
            Serial.println("GPS timestamp: unavailable");
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
