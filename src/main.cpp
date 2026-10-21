#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"
#include "Core/Time_Manager.h"


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

    // Keep this packet between loop iterations so its fields can be reused for each send.
    static BeaconPacket outgoingBeacon = {};
    static uint32_t lastBeaconTransmit = 0;

    const uint32_t now = millis();
    
    // Send periodically without delay(), so reception can still be checked between sends.
    if (now - lastBeaconTransmit >= BEACON_INTERVAL_MS)
    {
        generateBeacon(outgoingBeacon);
        transmitBeacon(outgoingBeacon);
        lastBeaconTransmit = now;
    }

    BeaconPacket incomingBeacon = {};
    if (receiveBeacon(incomingBeacon))
    {
        extractBeaconInformation(incomingBeacon);
    }
}
