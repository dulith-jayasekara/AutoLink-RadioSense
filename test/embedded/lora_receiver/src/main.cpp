#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

namespace
{
constexpr int PIN_SCK = 18;
constexpr int PIN_MISO = 19;
constexpr int PIN_MOSI = 23;
constexpr int PIN_NSS = 5;
constexpr int PIN_RESET = 14;
constexpr int PIN_DIO0 = 26;
constexpr long LORA_FREQUENCY = 433000000L;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr long LORA_BANDWIDTH = 125000L;
constexpr int LORA_CODING_RATE = 5;
constexpr int LORA_SYNC_WORD = 0x12;
constexpr long LORA_PREAMBLE_LENGTH = 8;
constexpr unsigned long INIT_RETRY_INTERVAL_MS = 2000;
constexpr size_t MAX_PACKET_BYTES = 255;

bool radioReady = false;
unsigned long nextInitializationAttempt = 0;
unsigned long packetNumber = 0;

void printHexByte(uint8_t value)
{
    const char *digits = "0123456789ABCDEF";
    Serial.print(digits[(value >> 4) & 0x0F]);
    Serial.print(digits[value & 0x0F]);
}

void printEscapedByte(uint8_t value)
{
    if (value == '\\')
    {
        Serial.print("\\\\");
    }
    else if (value == '\n')
    {
        Serial.print("\\n");
    }
    else if (value == '\r')
    {
        Serial.print("\\r");
    }
    else if (value == '\t')
    {
        Serial.print("\\t");
    }
    else if (value >= 32 && value <= 126)
    {
        Serial.print(static_cast<char>(value));
    }
    else
    {
        Serial.print("\\x");
        printHexByte(value);
    }
}

void configureRadioPins()
{
    pinMode(PIN_NSS, OUTPUT);
    digitalWrite(PIN_NSS, HIGH);
    pinMode(PIN_DIO0, INPUT);
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
    LoRa.setPins(PIN_NSS, PIN_RESET, PIN_DIO0);
}

bool initializeRadio()
{
    Serial.println("Initializing SX1278 LoRa receiver...");
    if (!LoRa.begin(LORA_FREQUENCY))
    {
        Serial.println("FAIL: LoRa initialization failed; no RF reception claimed.");
        return false;
    }

    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.enableCrc();

    Serial.println("PASS: SX1278 LoRa receiver initialized.");
    Serial.println("RF transmit operations are disabled.");
    return true;
}

void printPacket(const uint8_t *payload, size_t length)
{
    ++packetNumber;
    Serial.println();
    Serial.println("================ RECEIVED REAL RF PACKET ================");
    Serial.print("Packet number : ");
    Serial.println(packetNumber);
    Serial.print("Packet length : ");
    Serial.print(length);
    Serial.println(" bytes");

    Serial.print("Payload ASCII : ");
    for (size_t index = 0; index < length; ++index)
    {
        printEscapedByte(payload[index]);
    }
    Serial.println();

    Serial.print("Payload HEX   : ");
    for (size_t index = 0; index < length; ++index)
    {
        if (index > 0)
        {
            Serial.print(' ');
        }
        printHexByte(payload[index]);
    }
    Serial.println();
    Serial.print("RSSI          : ");
    Serial.print(LoRa.packetRssi());
    Serial.println(" dBm");
    Serial.print("SNR           : ");
    Serial.print(LoRa.packetSnr(), 2);
    Serial.println(" dB");
    Serial.println("==========================================================");
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("==========================================================");
    Serial.println(" SX1278 Ra-02 433 MHz RECEIVE-ONLY DIAGNOSTIC");
    Serial.println(" Board: ESP32 DOIT DevKit V1");
    Serial.println(" RF TRANSMISSION: DISABLED");
    Serial.println("==========================================================");

    configureRadioPins();
    radioReady = initializeRadio();
    nextInitializationAttempt = millis() + INIT_RETRY_INTERVAL_MS;
}

void loop()
{
    if (!radioReady)
    {
        const unsigned long now = millis();
        if (static_cast<long>(now - nextInitializationAttempt) >= 0)
        {
            radioReady = initializeRadio();
            nextInitializationAttempt = now + INIT_RETRY_INTERVAL_MS;
        }
        return;
    }

    const int packetSize = LoRa.parsePacket();
    if (packetSize <= 0)
    {
        return;
    }

    uint8_t payload[MAX_PACKET_BYTES] = {};
    size_t length = 0;
    while (LoRa.available())
    {
        const int value = LoRa.read();
        if (value < 0)
        {
            break;
        }
        if (length < sizeof(payload))
        {
            payload[length++] = static_cast<uint8_t>(value);
        }
    }

    printPacket(payload, length);
}
