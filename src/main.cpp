#include "Core/LoRa_Manager.h"
#include <Arduino.h>

// put your setup code here, to run once:
void setup()
{
    // Start Serial Monitor
    Serial.begin(115200);
    delay(10000);

    // Initialize LoRa module
    LoRa_Initialization();
}


void loop() {
  // put your main code here, to run repeatedly:
}

