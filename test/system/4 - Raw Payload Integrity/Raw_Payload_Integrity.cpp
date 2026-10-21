#include "Project_Headers.h"
#include "Library_Headers.h"
#include "Config.h"


// =====================================================
// DEVICE CONFIGURATION
// =====================================================

// Select the role for this ESP32.
#define DEVICE_ROLE_TX  1
#define DEVICE_ROLE_RX  2

#define DEVICE_ROLE DEVICE_ROLE_TX  // Change to DEVICE_ROLE_RX on receiver

// =====================================================
// TEST CONFIGURATION
// =====================================================

constexpr size_t TEST_PAYLOAD_SIZE = 64;
constexpr unsigned long TX_INTERVAL_MS = 2000;

// =====================================================
// TEST STATISTICS
// =====================================================

unsigned long packetsSent = 0;
unsigned long packetsReceived = 0;
unsigned long packetsPassed = 0;
unsigned long packetsFailed = 0;

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

void transmitTestPayload()
{
    uint8_t payload[TEST_PAYLOAD_SIZE];

    for (size_t i = 0; i < TEST_PAYLOAD_SIZE; i++)
    {
        payload[i] = generateTestByte(i);
    }

    Serial.println("\n--- RAW PAYLOAD TRANSMISSION ---");

    LoRa.beginPacket();

    size_t bytesWritten = LoRa.write(payload, TEST_PAYLOAD_SIZE);

    int txStatus = LoRa.endPacket();

    if (txStatus == 1 && bytesWritten == TEST_PAYLOAD_SIZE)
    {
        packetsSent++;
        Serial.printf("TX Status       : SUCCESS\n");
    }
    else
    {
        Serial.printf("TX Status       : FAILED\n");
    }

    Serial.printf("Payload Size    : %u bytes\n",
                  static_cast<unsigned int>(TEST_PAYLOAD_SIZE));

    Serial.printf("Bytes Written   : %u\n",
                  static_cast<unsigned int>(bytesWritten));

    Serial.printf("Packets Sent    : %lu\n", packetsSent);
}

// =====================================================
// RECEIVER
// =====================================================

void receiveTestPayload()
{
    int packetSize = LoRa.parsePacket();

    if (packetSize <= 0)
    {
        return;
    }

    packetsReceived++;

    uint8_t payload[TEST_PAYLOAD_SIZE];

    size_t bytesRead = 0;
    bool overflow = false;

    while (LoRa.available())
    {
        int value = LoRa.read();

        if (value < 0)
        {
            break;
        }

        if (bytesRead < TEST_PAYLOAD_SIZE)
        {
            payload[bytesRead] = static_cast<uint8_t>(value);
        }
        else
        {
            overflow = true;
        }

        bytesRead++;
    }

    bool integrityOK = true;

    if (packetSize != static_cast<int>(TEST_PAYLOAD_SIZE) ||
        bytesRead != TEST_PAYLOAD_SIZE ||
        overflow)
    {
        integrityOK = false;
    }
    else
    {
        for (size_t i = 0; i < TEST_PAYLOAD_SIZE; i++)
        {
            if (payload[i] != generateTestByte(i))
            {
                integrityOK = false;
                break;
            }
        }
    }

    Serial.println("\n--- RAW PAYLOAD RECEPTION ---");

    Serial.printf("Packet Size     : %d bytes\n", packetSize);
    Serial.printf("Bytes Read      : %u\n",
                  static_cast<unsigned int>(bytesRead));
    Serial.printf("RSSI            : %d dBm\n", LoRa.packetRssi());
    Serial.printf("SNR             : %.2f dB\n", LoRa.packetSnr());

    if (integrityOK)
    {
        packetsPassed++;
        Serial.println("Integrity       : PASS");
    }
    else
    {
        packetsFailed++;
        Serial.println("Integrity       : FAIL");
    }

    Serial.printf("Packets Received: %lu\n", packetsReceived);
    Serial.printf("Packets Passed  : %lu\n", packetsPassed);
    Serial.printf("Packets Failed  : %lu\n", packetsFailed);
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
    Serial.println(" Test 04: Raw Payload Integrity");
    Serial.println("==================================");

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
        transmitTestPayload();
    }

#elif DEVICE_ROLE == DEVICE_ROLE_RX

    receiveTestPayload();

#endif
}
