#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

#include "SCF_Buffer.h"
#include "SCF_Codec.h"
#include "SCF_DuplicateTracker.h"
#include "SCF_Radio_Adapter.h"

namespace
{
constexpr int PIN_SCK = 18;
constexpr int PIN_MISO = 19;
constexpr int PIN_MOSI = 23;
constexpr int PIN_NSS = 5;
constexpr int PIN_RESET = 14;
constexpr int PIN_DIO0 = 26;
constexpr long LORA_FREQUENCY = 433E6;
constexpr int LORA_SPREADING_FACTOR = 7;
constexpr long LORA_BANDWIDTH = 125E3;
constexpr int LORA_CODING_RATE = 5;
constexpr int LORA_SYNC_WORD = 0x12;
constexpr long LORA_PREAMBLE_LENGTH = 8;
constexpr uint32_t LOCAL_NODE_ID = 1;
constexpr uint32_t TEST_ORIGIN_ID = 0x54455354U;
constexpr uint32_t TEST_TTL_MS = 300000;
constexpr size_t MAX_PACKET_BYTES = 255;
constexpr unsigned long INIT_RETRY_INTERVAL_MS = 2000;

bool radioReady = false;
unsigned long nextInitializationAttempt = 0;
uint32_t receivedPacketNumber = 0;
uint32_t passed = 0;
uint32_t failed = 0;
scf::Buffer buffer;
scf::DuplicateTracker duplicateTracker;

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

void printPayload(const uint8_t *payload, size_t length)
{
    Serial.print("Raw payload: ");
    for (size_t index = 0; index < length; ++index)
    {
        printEscapedByte(payload[index]);
    }
    Serial.println();

    Serial.print("Payload hex: ");
    for (size_t index = 0; index < length; ++index)
    {
        if (index > 0)
        {
            Serial.print(' ');
        }
        printHexByte(payload[index]);
    }
    Serial.println();
}

void printCheck(bool condition, const char *label)
{
    if (condition)
    {
        ++passed;
        Serial.print("PASS: ");
    }
    else
    {
        ++failed;
        Serial.print("FAIL: ");
    }
    Serial.println(label);
}

const char *bufferResultName(scf::Buffer::EnqueueResult result)
{
    switch (result)
    {
    case scf::Buffer::EnqueueResult::Enqueued:
        return "Enqueued";
    case scf::Buffer::EnqueueResult::Invalid:
        return "Invalid";
    case scf::Buffer::EnqueueResult::Duplicate:
        return "Duplicate";
    case scf::Buffer::EnqueueResult::Full:
        return "Full";
    }
    return "Unknown";
}

const char *duplicateResultName(scf::DuplicateTracker::RememberResult result)
{
    switch (result)
    {
    case scf::DuplicateTracker::RememberResult::Added:
        return "Added";
    case scf::DuplicateTracker::RememberResult::Invalid:
        return "Invalid";
    case scf::DuplicateTracker::RememberResult::Duplicate:
        return "Duplicate";
    case scf::DuplicateTracker::RememberResult::Full:
        return "Full";
    }
    return "Unknown";
}

void printStoredMessage(const scf::Message &message)
{
    Serial.print("Stored ID: source=0x");
    Serial.print(message.bundleId.sourceId, HEX);
    Serial.print(" sequence=");
    Serial.println(message.bundleId.sequence);
    Serial.print("Stored payload: ");
    for (size_t index = 0; index < message.payloadLength; ++index)
    {
        printEscapedByte(message.payload[index]);
    }
    Serial.println();
    Serial.print("Stored expiresAt: ");
    Serial.println(message.expiresAt);
}

bool initializeRadio()
{
    Serial.println("Initializing receive-only LoRa radio...");
    if (!LoRa.begin(LORA_FREQUENCY))
    {
        Serial.println("FAIL: LoRa initialization failed; SCF storage is inactive.");
        return false;
    }

    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.enableCrc();
    Serial.println("PASS: LoRa initialized; receive polling is active.");
    Serial.println("RF transmission and ACK transmission are disabled.");
    return true;
}

bool decodeGenuineData(const uint8_t *payload, size_t length,
                       scf::Message &message)
{
    if (length < scf::FRAME_HEADER_SIZE || payload[0] != scf::FRAME_MAGIC)
    {
        return false;
    }

    uint32_t destination = 0;
    if (!scf::decodeData(payload, length, message, destination))
    {
        Serial.println("SCF-looking payload: codec validation FAILED; not inserted.");
        return false;
    }
    if (destination != LOCAL_NODE_ID && destination != scf::BROADCAST_ID)
    {
        Serial.print("Genuine SCF DATA rejected: destination=");
        Serial.println(destination);
        return false;
    }

    Serial.println("Genuine encoded SCF DATA detected; using encoded identity and TTL.");
    return true;
}

bool makeDiagnosticMessage(const uint8_t *payload, size_t length,
                           uint32_t now, scf::Message &message)
{
    if (length > scf::MAX_PAYLOAD_SIZE)
    {
        Serial.println("Diagnostic wrapping rejected: payload exceeds 64-byte SCF capacity.");
        return false;
    }

    message = {};
    message.version = scf::MODEL_VERSION;
    message.bundleId = {TEST_ORIGIN_ID, receivedPacketNumber};
    message.createdAt = now;
    message.expiresAt = now + TEST_TTL_MS;
    message.priority = scf::Priority::Normal;
    message.hopLimit = 4;
    return scf::setPayload(message, payload, length);
}

void expireBufferedMessages(uint32_t now)
{
    const size_t expired = buffer.expire(now);
    if (expired > 0)
    {
        Serial.print("TTL maintenance expired ");
        Serial.print(expired);
        Serial.print(" message(s); buffer count=");
        Serial.println(buffer.count());
    }

    if (buffer.count() == 0 && duplicateTracker.count() > 0)
    {
        duplicateTracker.clear();
        Serial.println("Diagnostic retention window ended: duplicate history cleared after buffer became empty.");
    }
}

void handlePacket(const uint8_t *payload, size_t length, int rssi, float snr)
{
    ++receivedPacketNumber;
    const uint32_t now = millis();

    Serial.println();
    Serial.println("================ RECEIVED REAL RF PACKET ================");
    Serial.print("Packet number=");
    Serial.print(receivedPacketNumber);
    Serial.print(" length=");
    Serial.println(length);
    printPayload(payload, length);
    Serial.print("RSSI=");
    Serial.print(rssi);
    Serial.print(" dBm SNR=");
    Serial.print(snr, 2);
    Serial.println(" dB");

    scf::Message message = {};
    const bool scfCandidate = length > 0 && payload[0] == scf::FRAME_MAGIC;
    const bool genuineScf = decodeGenuineData(payload, length, message);
    if (scfCandidate && !genuineScf)
    {
        Serial.println("SCF candidate rejected; raw diagnostic wrapping disabled for malformed frames.");
        return;
    }
    if (!genuineScf)
    {
        Serial.println("Diagnostic mode: wrapping raw payload; not claiming SCF interoperability.");
        if (!makeDiagnosticMessage(payload, length, now, message))
        {
            Serial.println("Buffer insertion skipped: diagnostic message invalid.");
            return;
        }
    }

    printCheck(scf::isValidMessage(message), "SCF_Message validation");
    if (!scf::isValidMessage(message))
    {
        Serial.println("Buffer insertion skipped: validation failed.");
        return;
    }

    if (duplicateTracker.contains(message.bundleId))
    {
        Serial.println("Duplicate tracker result: Duplicate");
        Serial.println("Buffer insertion skipped: duplicate identity.");
        return;
    }
    if (duplicateTracker.count() >= scf::DuplicateTracker::CAPACITY)
    {
        Serial.println("Duplicate tracker result: Full");
        Serial.println("Buffer insertion skipped: duplicate history is full.");
        return;
    }

    const scf::Buffer::EnqueueResult bufferResult = buffer.enqueue(message);
    Serial.print("Buffer insertion result: ");
    Serial.println(bufferResultName(bufferResult));
    Serial.print("Buffer count: ");
    Serial.println(buffer.count());

    if (bufferResult == scf::Buffer::EnqueueResult::Enqueued)
    {
        const scf::DuplicateTracker::RememberResult duplicateResult =
            duplicateTracker.remember(message.bundleId);
        Serial.print("Duplicate tracker result: ");
        Serial.println(duplicateResultName(duplicateResult));
        const scf::Message *stored = buffer.find(message.bundleId);
        printCheck(stored != nullptr, "message found in production SCF buffer");
        if (stored != nullptr)
        {
            printStoredMessage(*stored);
        }
    }
    else
    {
        Serial.println("Message was not admitted to the production SCF buffer.");
    }
    Serial.println("==========================================================");
}
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("==========================================================");
    Serial.println(" LoRa to Production SCF Buffer Diagnostic");
    Serial.println(" RECEIVE-ONLY / NO RF TRANSMISSION / NO ACK");
    Serial.println(" Raw text uses TEST-ONLY identity and 300,000 ms (5 minutes) TTL");
    Serial.println("==========================================================");

    pinMode(PIN_NSS, OUTPUT);
    digitalWrite(PIN_NSS, HIGH);
    pinMode(PIN_DIO0, INPUT);
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
    LoRa.setPins(PIN_NSS, PIN_RESET, PIN_DIO0);

    radioReady = initializeRadio();
    nextInitializationAttempt = millis() + INIT_RETRY_INTERVAL_MS;
}

void loop()
{
    const uint32_t now = millis();
    expireBufferedMessages(now);

    if (!radioReady)
    {
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

    handlePacket(payload, length, LoRa.packetRssi(), LoRa.packetSnr());
}
