#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <string.h>
#include "SCF_Message.h"
#include "SCF_Codec.h"
#include "SCF_Buffer.h"

#ifndef M12_SENDER
#error Select the sender or receiver PlatformIO environment
#endif

namespace
{
constexpr uint32_t SOURCE = 1;
constexpr uint32_t DESTINATION = 2;
constexpr uint32_t TTL_MS = 300000;
constexpr uint32_t COPY_DELAY_MS = 3000;
constexpr char PAYLOAD[] = "M12 native SCF hazard: road obstacle";
bool ready = false;
uint32_t failures = 0;

bool check(bool condition, const char *label)
{
    Serial.print(condition ? "PASS: " : "FAIL: ");
    Serial.println(label);
    if (!condition) ++failures;
    return condition;
}

void logMessage(const scf::Message &message, uint32_t destination)
{
    Serial.printf("Identity=(%lu,%lu) source=%lu destination=%lu version=%u\n",
                  static_cast<unsigned long>(message.bundleId.sourceId),
                  static_cast<unsigned long>(message.bundleId.sequence),
                  static_cast<unsigned long>(message.bundleId.sourceId),
                  static_cast<unsigned long>(destination), message.version);
    Serial.printf("createdAt=%lu expiresAt=%lu TTL=%lu ms hopCount=%u hopLimit=%u priority=%u\n",
                  static_cast<unsigned long>(message.createdAt),
                  static_cast<unsigned long>(message.expiresAt),
                  static_cast<unsigned long>(message.expiresAt - message.createdAt),
                  message.hopCount, message.hopLimit, static_cast<unsigned>(message.priority));
    Serial.print("Payload: ");
    Serial.write(message.payload, message.payloadLength);
    Serial.println();
}

#if M12_SENDER
uint8_t frame[scf::MAX_DATA_FRAME_SIZE];
size_t frameLength = 0;
bool started = false;
bool copyPending = false;
uint32_t initialSentAt = 0;

bool sendFrame()
{
    if (!check(LoRa.beginPacket() == 1, "radio packet opened")) return false;
    if (!check(LoRa.write(frame, frameLength) == frameLength, "complete encoded DATA written"))
    {
        LoRa.idle();
        return false;
    }
    // Synchronous completion is intentional in this small isolated diagnostic.
    const bool sent = check(LoRa.endPacket() == 1, "local RF DATA transmission completed");
    Serial.println("Sender RSSI/SNR: N/A; receiver reports received signal metrics.");
    Serial.println("Local completion does not prove reception.");
    return sent;
}

void senderLoop()
{
    if (!started && Serial.available() && Serial.read() == 's')
    {
        started = true;
        scf::Message message = {};
        message.version = scf::MODEL_VERSION;
        message.bundleId = {SOURCE, 1};
        message.createdAt = 0;
        message.expiresAt = TTL_MS;
        message.priority = scf::Priority::High;
        message.hopCount = 0;
        message.hopLimit = 4;
        if (!check(scf::setPayload(message, reinterpret_cast<const uint8_t *>(PAYLOAD),
                                   sizeof(PAYLOAD) - 1) && scf::isValidMessage(message),
                   "production SCF_Message valid")) return;
        if (!check(scf::encodeData(message, DESTINATION, frame, sizeof(frame), frameLength),
                   "production DATA codec encoding")) return;
        logMessage(message, DESTINATION);
        Serial.printf("Encoded DATA length=%u bytes\n", static_cast<unsigned>(frameLength));
        if (sendFrame())
        {
            initialSentAt = millis();
            copyPending = true;
        }
    }
    if (copyPending && static_cast<uint32_t>(millis() - initialSentAt) >= COPY_DELAY_MS)
    {
        copyPending = false;
        Serial.println("TEST: intentional identical DATA copy; no ACK/retry protocol");
        sendFrame();
        Serial.println(failures == 0 ? "PASS: M12 SENDER PAIR COMPLETE (local evidence only)"
                                     : "FAIL: M12 SENDER PAIR");
    }
}
#else
scf::Buffer buffer;
bool accepted = false;
bool duplicateObserved = false;
bool expiryObserved = false;

const char *admissionName(scf::Buffer::EnqueueResult result)
{
    switch (result)
    {
    case scf::Buffer::EnqueueResult::Enqueued: return "Enqueued";
    case scf::Buffer::EnqueueResult::Duplicate: return "Duplicate";
    case scf::Buffer::EnqueueResult::Full: return "Full";
    default: return "Invalid";
    }
}

void receiverLoop()
{
    const uint32_t now = millis();
    if (buffer.expire(now) > 0)
    {
        expiryObserved = true;
        check(buffer.empty(), "production buffer TTL expiry removed stored message");
    }
    const int packetSize = LoRa.parsePacket();
    if (packetSize <= 0) return;
    const int rssi = LoRa.packetRssi();
    const float snr = LoRa.packetSnr();
    Serial.println("=== M12 RECEIVED REAL RF PACKET ===");
    Serial.printf("RF length=%d RSSI=%d dBm SNR=%.2f dB\n", packetSize, rssi, snr);
    uint8_t bytes[scf::MAX_DATA_FRAME_SIZE] = {};
    size_t length = 0;
    bool readError = false;
    while (LoRa.available())
    {
        const int value = LoRa.read();
        if (value < 0) { readError = true; break; }
        if (length < sizeof(bytes)) bytes[length] = static_cast<uint8_t>(value);
        ++length;
    }
    if (!check(!readError && length == static_cast<size_t>(packetSize) && length <= sizeof(bytes),
               "bounded complete RF frame")) return;
    scf::Message wire = {};
    uint32_t destination = 0;
    if (!check(scf::decodeData(bytes, length, wire, destination), "production DATA codec decoding")) return;
    logMessage(wire, destination);
    if (!check(scf::isValidMessage(wire) && wire.bundleId.sourceId == SOURCE &&
               wire.bundleId.sequence == 1 && destination == DESTINATION &&
               wire.createdAt == 0 && wire.expiresAt == TTL_MS &&
               wire.priority == scf::Priority::High && wire.hopCount == 0 && wire.hopLimit == 4 &&
               wire.payloadLength == sizeof(PAYLOAD) - 1 &&
               memcmp(wire.payload, PAYLOAD, sizeof(PAYLOAD) - 1) == 0,
               "all expected M12 DATA fields and payload")) return;

    // Local copy only: never compare the sender's clock to receiver millis().
    // Buffer uses ordinary unsigned comparisons; refuse a deadline that wraps.
    const uint32_t receivedAt = millis();
    if (!check(receivedAt <= UINT32_MAX - TTL_MS, "local TTL deadline cannot wrap")) return;
    scf::Message local = wire;
    local.createdAt = receivedAt;
    local.expiresAt = receivedAt + TTL_MS;
    const size_t before = buffer.count();
    const scf::Message *previous = buffer.find(local.bundleId);
    const uint32_t previousExpiry = previous ? previous->expiresAt : 0;
    const auto result = buffer.enqueue(local);
    Serial.printf("Buffer admission=%s count=%u capacity=%u localCreatedAt=%lu localExpiresAt=%lu\n",
                  admissionName(result), static_cast<unsigned>(buffer.count()),
                  static_cast<unsigned>(scf::Buffer::CAPACITY),
                  static_cast<unsigned long>(local.createdAt), static_cast<unsigned long>(local.expiresAt));
    if (!accepted)
    {
        accepted = check(result == scf::Buffer::EnqueueResult::Enqueued && buffer.count() == 1 &&
                         buffer.find(local.bundleId) != nullptr, "first RF DATA admitted to production buffer");
    }
    else if (!expiryObserved)
    {
        const scf::Message *stored = buffer.find(local.bundleId);
        duplicateObserved = check(result == scf::Buffer::EnqueueResult::Duplicate &&
                                  buffer.count() == before && stored && stored->expiresAt == previousExpiry,
                                  "repeated identity rejected without refreshing TTL");
    }
    else
    {
        Serial.println("INFO: post-expiry reception; reset both nodes for a fresh duplicate test.");
    }
    if (accepted && duplicateObserved && failures == 0)
        Serial.println("PASS: M12 RF DATA ADMISSION AND DUPLICATE REJECTION");
}
#endif
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println(M12_SENDER ? "=== M12 NATIVE SCF DATA SENDER ===" : "=== M12 NATIVE SCF DATA RECEIVER ===");
    Serial.println("Diagnostic-only reception-relative TTL: 300000 ms (5 minutes).");
    Serial.println("No ACK/NACK, reliability retries, or multi-hop.");
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    pinMode(26, INPUT);
    SPI.begin(18, 19, 23, 5);
    LoRa.setPins(5, 14, 26);
    ready = check(LoRa.begin(433000000L), "SX1278 initialization at 433 MHz");
    if (!ready) return;
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125000L);
    LoRa.setCodingRate4(5);
    LoRa.setSyncWord(0x12);
    LoRa.setPreambleLength(8);
    LoRa.enableCrc();
    LoRa.setTxPower(10);
    Serial.println("Radio: SF7 BW125000 CR4/5 sync=0x12 preamble=8 CRC=on TX=10dBm");
    Serial.println(M12_SENDER ? "Ready: type lowercase s to send one DATA pair; reset to repeat."
                             : "Ready: receive-only; waiting for actual native DATA RF frames.");
}

void loop()
{
    if (!ready) return;
#if M12_SENDER
    senderLoop();
#else
    receiverLoop();
#endif
}
