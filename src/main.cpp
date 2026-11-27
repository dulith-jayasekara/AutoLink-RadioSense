#include "Project_Headers.h"

HazardRepository hazardRepository;

// put your setup code here, to run once:
void setup()
{
    // Start Serial Monitor
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);

    // Time is invalid after every restart until a phone synchronizes it.
    Time_Initialization();

    // Initialize LoRa module
    LoRa_Initialization();

    // Start with an empty neighbour table after every boot.
    initializeNeighbourTable();

    // Initialize Wi-Fi connectivity
    WiFi_Initialization();
    CaptivePortal_Initialization();

    // Temporary first-step demonstration: store and retrieve one RAM record.
    const HazardRecord sampleHazard = {
        1,
        HazardType::RoadObstacle,
        6.9271,
        79.8612,
        0,
        0,
        1,
        50,
        HazardStatus::Unverified};

    Serial.println("\nHazard Memory repository test");
    if (hazardRepository.add(sampleHazard))
    {
        const HazardRecord *storedHazard = hazardRepository.findById(sampleHazard.id);
        if (storedHazard != nullptr)
        {
            Serial.print("Stored hazard ID: ");
            Serial.println(storedHazard->id);
            Serial.print("Latitude: ");
            Serial.println(storedHazard->latitude, 6);
            Serial.print("Longitude: ");
            Serial.println(storedHazard->longitude, 6);
            Serial.print("Repository records: ");
            Serial.println(hazardRepository.count());
        }
    }
    else
    {
        Serial.println("Failed to add sample hazard.");
    }
}


void loop() {
    // Process phone requests before generating a beacon so a newly received
    // location is available to the next scheduled transmission.
    CaptivePortal_HandleClient();
    GPS_UpdateFreshness();

    // Expire entries even when no new beacons arrive.
    removeExpiredNeighbours();

    GPSMeasurementSnapshot localSnapshot = {};
    GPS_GetSnapshot(localSnapshot);
    uint16_t currentSpeed = GPS_SPEED_UNAVAILABLE;
    bool speedAvailable = localSnapshot.speedAvailable;
    if (speedAvailable)
    {
        const double encodedSpeed =
            localSnapshot.speedMetersPerSecond * 100.0;
        if (isfinite(encodedSpeed) &&
            encodedSpeed >= 0.0 &&
            encodedSpeed < static_cast<double>(GPS_SPEED_UNAVAILABLE))
        {
            currentSpeed = static_cast<uint16_t>(encodedSpeed + 0.5);
        }
        else
        {
            speedAvailable = false;
        }
    }

    // Manage adaptive beacon transmission.
    handleBeaconTransmission(
        currentSpeed,
        speedAvailable);

    BeaconPacket incomingBeacon = {};
    BeaconReceptionInfo receptionInfo = {};
    if (receiveBeacon(incomingBeacon, receptionInfo))
    {
        uint16_t sequenceDifference = 0;
        BeaconGPSUpdate gpsUpdate = BeaconGPSUpdate::Invalidate;
        if (validateReceivedBeacon(
                incomingBeacon,
                sequenceDifference,
                gpsUpdate))
        {
            extractBeaconInformation(incomingBeacon);

            double localLatitude = 0.0;
            double localLongitude = 0.0;
            const bool hasLocalLocation =
                GPS_GetLocation(localLatitude, localLongitude);

            const int32_t localLatitudeScaled = hasLocalLocation
                ? static_cast<int32_t>(localLatitude * 10000000.0)
                : GPS_LATITUDE_UNAVAILABLE;
            const int32_t localLongitudeScaled = hasLocalLocation
                ? static_cast<int32_t>(localLongitude * 10000000.0)
                : GPS_LONGITUDE_UNAVAILABLE;

            if (addOrUpdateNeighbour(
                    incomingBeacon.nodeID,
                    incomingBeacon.sequenceNumber,
                    sequenceDifference,
                    gpsUpdate,
                    incomingBeacon.timestamp,
                    incomingBeacon.latitude,
                    incomingBeacon.longitude,
                    incomingBeacon.heading,
                    incomingBeacon.speed,
                    receptionInfo.rssi,
                    receptionInfo.snr,
                    localLatitudeScaled,
                    localLongitudeScaled))
            {
                printNeighbourTable();
            }
        }
    }
}
