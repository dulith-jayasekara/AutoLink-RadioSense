#include "Project_Headers.h"
#include "Library_Headers.h"

void LoRa_Initialization()
{
    int attempts = 0;

    Serial.println();
    Serial.println("========================================");
    Serial.println("         LoRa Initialization");
    Serial.println("========================================");
    
    //Initialize SPI
    Serial.println("Initializing SPI...");
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    
    //Configure LoRa control pins
    Serial.println("Initializing LoRa radio...");
    LoRa.setPins(LORA_NSS, LORA_RST, LORA_DI0);

    //Initialize LoRa Transceiver module
    while(!LoRa.begin(FREQ)){
        Serial.print(".");
        delay(500);
        attempts++;

        if(attempts > 10){
            Serial.println("\nLoRa init failed! Halting program.");
            while (true); // stop execution
        }
    }

    //Set LoRa parameters
    LoRa.setSpreadingFactor(SPREADING_FACTOR);
    LoRa.setSignalBandwidth(BANDWIDTH);
    LoRa.setCodingRate4(CODING_RATE);
    LoRa.setSyncWord(SYNC_WORD);
    LoRa.setTxPower(TX_POWER);
    LoRa.setPreambleLength(PREAMBLE_LENGTH);
    
    //Enable packet CRC
    LoRa.enableCrc();

    //Display LoRa configuration
    Serial.println();
    Serial.println("LoRa transciever initialized successfully!");

    Serial.println("   Frequency: "        + String(FREQ / 1E6) + " MHz");
    Serial.println("   Spreading Factor: " + String(SPREADING_FACTOR));
    Serial.println("   Bandwidth: "        + String(BANDWIDTH / 1E3) + " kHz");
    Serial.println("   Coding Rate: 4/"    + String(CODING_RATE));
    Serial.println("   Sync Word: 0x"      + String(SYNC_WORD, HEX));
    Serial.println("   Transmit Power: "   + String(TX_POWER) + " dBm");
    Serial.println("   Preamble Length: "  + String(PREAMBLE_LENGTH));
    Serial.println("   CRC: Enabled");

    Serial.println("========================================");
}