#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"

// --------------------------------------------------
// Raw LoRa Transmission System Test
// AutoLink RadioSense
// --------------------------------------------------

const char* TEST_MESSAGE = "Hello from AutoLink RadioSense!";
unsigned long packetNumber = 0;

void setup(){
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" AutoLink RadioSense");
    Serial.println(" System Test 01 - Raw Transmission");
    Serial.println("========================================");
    Serial.println();

    //Existing LoRa initialization function from LoRa_Manager.cpp
    LoRa_Initialization();

    Serial.println();
    Serial.println("LoRa initialization completed.");
    Serial.println("Starting raw packet transmission...");
    Serial.println();
}

void loop()
{
    packetNumber++;

    Serial.println("----------------------------------------");
    Serial.print("Packet #");
    Serial.println(packetNumber);

    Serial.print("Payload: ");
    Serial.println(TEST_MESSAGE);

    Serial.println("Transmitting...");

    // Start packet
    LoRa.beginPacket();

    // Write raw payload
    LoRa.print(TEST_MESSAGE);

    // Finish and transmit packet
    int result = LoRa.endPacket();

    if (result == 1){
        Serial.println("Packet transmitted SUCCEEDED.");
    }
    else
    {
        Serial.println("Packet transmission FAILED.");
    }

    Serial.println();

    // Wait before sending next packet
    delay(5000);
}