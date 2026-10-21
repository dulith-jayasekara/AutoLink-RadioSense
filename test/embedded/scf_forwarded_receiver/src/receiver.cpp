#include "ForwardingDiagnostic.h"

bool ready = false;
uint32_t receivedPackets = 0;

void setup()
{
    ready = forwarding::initialize();
    Serial.println("NODE C: SCF DATA receiver (Beacon, text, binary), RF validation required");
}

void loop()
{
    if (!ready) return;
    uint8_t bytes[scf::MAX_DATA_FRAME_SIZE];
    size_t length = 0;
    if (!forwarding::receive(bytes, sizeof(bytes), length)) return;
    ++receivedPackets;
    Serial.println("==================================================");
    Serial.printf("NODE C - RECEIVED RF FRAME #%lu (%u bytes)\n",
                  static_cast<unsigned long>(receivedPackets), static_cast<unsigned>(length));

    scf::Message message = {};
    uint32_t destination = 0;
    if (!forwarding::check(scf::decodeData(bytes, length, message, destination),
                           "production SCF DATA decoded from RF"))
    {
        Serial.println("INFO: not a valid SCF DATA frame; ignored");
        return;
    }
    forwarding::identity(message);
    if (!forwarding::check(destination == forwarding::DESTINATION &&
                           message.bundleId.sourceId == forwarding::ORIGIN &&
                           message.bundleId.sequence != 0 &&
                           message.hopCount == 1 && message.hopLimit == 4 &&
                           message.priority == scf::Priority::Normal &&
                           message.createdAt == 0 &&
                           message.expiresAt > 0 && message.expiresAt <= forwarding::TTL,
                           "destination, diagnostic identity, hop and TTL validated")) return;

    Serial.printf("Original Payload Length: %u bytes\n", message.payloadLength);
    forwarding::hex(message.payload, message.payloadLength);

    beacon::Packet packet = {};
    if (message.payloadLength == beacon::PACKET_SIZE &&
        beacon::decode(message.payload, message.payloadLength, packet) == beacon::Result::Ok)
    {
        Serial.println("Original Type : BEACON");
        forwarding::beaconLog(packet);
    }
    else
    {
        Serial.println("Original Type : RAW / UNKNOWN");
        bool printable = message.payloadLength > 0;
        for (size_t i = 0; i < message.payloadLength; ++i)
            if (message.payload[i] < 32 || message.payload[i] > 126) printable = false;
        Serial.print("Original Text : ");
        if (printable)
        {
            Serial.write(message.payload, message.payloadLength);
            Serial.println();
        }
        else Serial.println("<binary / non-printable>");
    }
    Serial.printf("Remaining TTL : %lu ms (transit age unknown)\n",
                  static_cast<unsigned long>(message.expiresAt));
    Serial.println("PASS: NODE C RECEIVED VALID FORWARDED SCF DATA; COMPARE HEX AND ID WITH NODE B");
    Serial.println("==================================================\n");
}
