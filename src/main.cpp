#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"
#include "Hazard Memory Network/Hazard_Repository.h"

HazardRepository hazardRepository;

// put your setup code here, to run once:
void setup()
{
    // Start Serial Monitor
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);

    // Initialize LoRa module
    LoRa_Initialization();

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
  // put your main code here, to run repeatedly:
}
