#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"

// put your setup code here, to run once:
void setup()
{
    // Start Serial Monitor
    Serial.begin(SERIAL_BAUD_RATE);
    delay(START_DELAY);

    // Initialize LoRa module
    LoRa_Initialization();
}


void loop() {
  // put your main code here, to run repeatedly:
}

