#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"

// =====================================================
// DEVICE CONFIGURATION
// =====================================================

#define DEVICE_ROLE_TX  1
#define DEVICE_ROLE_RX  2

#define DEVICE_ROLE DEVICE_ROLE_TX  // Change to DEVICE_ROLE_RX on receiver

// =====================================================
// TEST CONFIGURATION
// =====================================================

// Override this value using PlatformIO build_flags if needed.
#ifndef TEST_PAYLOAD_SIZE
#define TEST_PAYLOAD_SIZE 32
#endif

constexpr size_t PAYLOAD_SIZE = TEST_PAYLOAD_SIZE;
constexpr unsigned long TX_INTERVAL_MS = 2000;

static_assert(
    PAYLOAD_SIZE >= 1 && PAYLOAD_SIZE <= 200,
    "TEST_PAYLOAD_SIZE must be between 1 and 200 bytes"
);

// =====================================================
// TEST STATISTICS
// =====================================================

unsigned long packetsSent = 0;
unsigned long packetsReceived = 0;
unsigned long lengthMatches = 0;
unsigned long payloadsPassed = 0;
unsigned long payloadsFailed = 0;

// =====================================================
// PAYLOAD GENERATION
// =====================================================

uint8_t generateTestByte(size_t index)
{
    return static_cast<uint8_t>((index * 37 + 11) & 0xFF);
}

// =====================================================
// TRANSMITTER
// =====================================================

void transmitTestPacket()
{
    uint8_t payload[PAYLOAD_SIZE];

    for (size_t i = 0; i < PAYLOAD_SIZE; i++)
    {
        payload[i] = generateTestByte(i);
    }

    Serial.println("\n--- RAW PACKET SIZE TRANSMISSION ---");

    LoRa.beginPacket();

    size_t bytesWritten = LoRa.write(payload, PAYLOAD_SIZE);

    unsigned long startTime = micros();
    int txStatus = LoRa.endPacket();
    unsigned long txDuration = micros() - startTime;

    if (txStatus == 1 && bytesWritten == PAYLOAD_SIZE)
    {
        packetsSent++;
        Serial.println("TX Status       : SUCCESS");
    }
    else
    {
        Serial.println("TX Status       : FAILED");
    }

    Serial.printf("Payload Size    : %u bytes\n",
                  static_cast<unsigned int>(PAYLOAD_SIZE));

    Serial.printf("Bytes Written   : %u\n",
                  static_cast<unsigned int>(bytesWritten));

    Serial.printf("TX Call Duration: %lu us\n", txDuration);
    Serial.printf("Packets Sent    : %lu\n", packetsSent);
}

// =====================================================
// RECEIVER
// =====================================================

void receiveTestPacket()
{
    int packetSize = LoRa.parsePacket();

    if (packetSize <= 0)
    {
        return;
    }

    packetsReceived++;

    size_t bytesRead = 0;
    bool overflow = false;
    bool payloadOK = true;

    while (LoRa.available())
    {
        int value = LoRa.read();

        if (value < 0)
        {
            break;
        }

        if (bytesRead >= PAYLOAD_SIZE)
        {
            overflow = true;
            payloadOK = false;
        }
        else if (static_cast<uint8_t>(value) != generateTestByte(bytesRead))
        {
            payloadOK = false;
        }

        bytesRead++;
    }

    bool lengthOK =
        packetSize == static_cast<int>(PAYLOAD_SIZE) &&
        bytesRead == PAYLOAD_SIZE &&
        !overflow;

    if (lengthOK)
    {
        lengthMatches++;
    }
    else
    {
        payloadOK = false;
    }

    Serial.println("\n--- RAW PACKET SIZE RECEPTION ---");

    Serial.printf("Expected Size   : %u bytes\n",
                  static_cast<unsigned int>(PAYLOAD_SIZE));

    Serial.printf("Packet Size     : %d bytes\n", packetSize);

    Serial.printf("Bytes Read      : %u\n",
                  static_cast<unsigned int>(bytesRead));

    Serial.printf("RSSI            : %d dBm\n", LoRa.packetRssi());
    Serial.printf("SNR             : %.2f dB\n", LoRa.packetSnr());

    Serial.printf("Length Check    : %s\n",
                  lengthOK ? "PASS" : "FAIL");

    Serial.printf("Payload Check   : %s\n",
                  payloadOK ? "PASS" : "FAIL");

    if (lengthOK && payloadOK)
    {
        payloadsPassed++;
    }
    else
    {
        payloadsFailed++;
    }

    Serial.printf("Packets Received: %lu\n", packetsReceived);
    Serial.printf("Length Matches  : %lu\n", lengthMatches);
    Serial.printf("Payloads Passed : %lu\n", payloadsPassed);
    Serial.printf("Payloads Failed : %lu\n", payloadsFailed);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==================================");
    Serial.println(" AutoLink RadioSense");
    Serial.println(" Test 05: Raw Packet Size");
    Serial.println("==================================");

    Serial.printf("Configured Payload Size: %u bytes\n",
                  static_cast<unsigned int>(PAYLOAD_SIZE));

    LoRa_Initialization();

#if DEVICE_ROLE == DEVICE_ROLE_TX
    Serial.println("Device Role: TRANSMITTER");
#elif DEVICE_ROLE == DEVICE_ROLE_RX
    Serial.println("Device Role: RECEIVER");
#else
#error "Invalid DEVICE_ROLE selected"
#endif
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop()
{
#if DEVICE_ROLE == DEVICE_ROLE_TX

    static unsigned long lastTransmission = 0;

    if (millis() - lastTransmission >= TX_INTERVAL_MS)
    {
        lastTransmission = millis();
        transmitTestPacket();
    }

#elif DEVICE_ROLE == DEVICE_ROLE_RX

    receiveTestPacket();

#endif
}
