
#include "ForwardingDiagnostic.h"
#include "SCF_Buffer.h"
#include "SCF_DuplicateTracker.h"

#include <cstring>
#include <cstdint>

namespace
{
bool ready = false;
scf::Buffer buffer;
scf::DuplicateTracker history;
uint32_t sequence = 0;
uint32_t receivedPackets = 0;

const char *SEPARATOR = "==================================================";

struct ReportFooter
{
    ~ReportFooter()
    {
        Serial.println("\nSCF BUFFER\n----------");
        Serial.printf(
            "Stored        : %u / %u\n"
            "TTL           : %lu ms\n"
            "History       : %u / %u\n",
            static_cast<unsigned>(buffer.count()),
            static_cast<unsigned>(scf::Buffer::CAPACITY),
            static_cast<unsigned long>(forwarding::TTL),
            static_cast<unsigned>(history.count()),
            static_cast<unsigned>(scf::DuplicateTracker::CAPACITY)
        );
        Serial.println(SEPARATOR);
        Serial.println();
        Serial.println();
    }
};

bool status(bool ok, const char *label, const char *failure)
{
    Serial.printf(
        "%-14s: %s%s%s\n",
        label,
        ok ? "PASS" : "FAIL",
        ok ? "" : " - ",
        ok ? "" : failure
    );
    return ok;
}

const char *bufferFailure(scf::Buffer::EnqueueResult result)
{
    switch (result)
    {
    case scf::Buffer::EnqueueResult::Full:
        return "BUFFER FULL";
    case scf::Buffer::EnqueueResult::Duplicate:
        return "DUPLICATE IDENTITY";
    case scf::Buffer::EnqueueResult::Invalid:
        return "INVALID MESSAGE";
    default:
        return "UNKNOWN";
    }
}

const char *historyFailure(scf::DuplicateTracker::RememberResult result)
{
    switch (result)
    {
    case scf::DuplicateTracker::RememberResult::Full:
        return "HISTORY FULL";
    case scf::DuplicateTracker::RememberResult::Duplicate:
        return "DUPLICATE IDENTITY";
    case scf::DuplicateTracker::RememberResult::Invalid:
        return "INVALID IDENTITY";
    default:
        return "UNKNOWN";
    }
}

void printIdentity(const scf::Message &message)
{
    Serial.printf(
        "SCF Source    : %lu (synthetic TEST origin)\n"
        "SCF Sequence  : %lu\n"
        "Hop Count     : %u / %u\n",
        static_cast<unsigned long>(message.bundleId.sourceId),
        static_cast<unsigned long>(message.bundleId.sequence),
        message.hopCount,
        message.hopLimit
    );
}

void printHeader(const char *title, size_t length, int rssi, float snr)
{
    Serial.println(SEPARATOR);
    Serial.print("             NODE B - ");
    Serial.println(title);
    Serial.println(SEPARATOR);

    Serial.printf(
        "\nPacket Number : %03lu\n"
        "Payload Size  : %u bytes\n",
        static_cast<unsigned long>(receivedPackets),
        static_cast<unsigned>(length)
    );

    Serial.println("\nRF INFORMATION\n--------------");
    Serial.printf("RSSI          : %d dBm\n", rssi);
    Serial.printf("SNR           : %.2f dB\n", snr);
}

// Read a complete LoRa frame without interpreting its structure.
// Oversized frames are drained but never passed to the decoder.
bool receivePacket(
    uint8_t *bytes,
    size_t capacity,
    size_t &length,
    bool &complete)
{
    const int packetSize = LoRa.parsePacket();

    if (packetSize <= 0)
        return false;

    length = 0;
    bool readOk = true;

    while (LoRa.available())
    {
        const int value = LoRa.read();

        if (value < 0)
        {
            readOk = false;
            break;
        }

        if (length < capacity)
            bytes[length] = static_cast<uint8_t>(value);

        ++length;
    }

    complete =
        readOk &&
        length == static_cast<size_t>(packetSize) &&
        length <= capacity;

    return true;
}

void printRawPacket(
    const uint8_t *bytes,
    size_t length,
    bool complete,
    int rssi,
    float snr)
{
    printHeader("RAW / UNKNOWN", length, rssi, snr);

    Serial.println("\nPACKET CLASSIFICATION\n---------------------");
    Serial.println("Packet Type   : RAW / UNKNOWN");

    Serial.println("\nORIGINAL PAYLOAD\n----------------");

    if (!complete)
    {
        Serial.println("Frame Status  : INCOMPLETE / OVERSIZED");
        Serial.println("Text          : NOT AVAILABLE");
        Serial.println("HEX           : NOT FULLY CAPTURED");

        Serial.println("\nSCF PROCESSING\n--------------");
        Serial.println("SCF Buffer    : processing follows below");

        Serial.println(SEPARATOR);
        Serial.println();
        Serial.println();
        return;
    }

    bool printable = length > 0;

    for (size_t i = 0; i < length; ++i)
    {
        if (bytes[i] < 32 || bytes[i] > 126)
        {
            printable = false;
            break;
        }
    }

    Serial.print("Text          : ");

    if (printable)
    {
        for (size_t i = 0; i < length; ++i)
            Serial.write(bytes[i]);

        Serial.println();
    }
    else
    {
        Serial.println("<binary / non-printable>");
    }

    Serial.println("HEX           :");
    forwarding::hex(bytes, length);

    Serial.println("\nSCF PROCESSING\n--------------");
    Serial.println("Beacon Decode : NOT A VALID BEACON");
    Serial.println("SCF Buffer    : processing follows below");

    Serial.println(SEPARATOR);
    Serial.println();
    Serial.println();
}

void forward()
{
    Serial.println(SEPARATOR);
    Serial.println("             NODE B - MANUAL FORWARD");
    Serial.println(SEPARATOR);
    Serial.println();

    ReportFooter footer;

    const uint32_t now = millis();

    buffer.expire(now);

    const scf::Message *stored = buffer.selectNext(now);

    if (!status(
            stored != nullptr,
            "Selection",
            "NO ELIGIBLE STORED MESSAGE"))
        return;

    if (!status(
            stored->hopCount < stored->hopLimit,
            "Hop Budget",
            "HOP LIMIT REACHED"))
        return;

    scf::Message outgoing = *stored;

    // Preserve remaining TTL for this diagnostic forwarding test.
    outgoing.createdAt = 0;
    outgoing.expiresAt = stored->expiresAt - now;
    ++outgoing.hopCount;

    uint8_t frame[scf::MAX_DATA_FRAME_SIZE];
    size_t length = 0;

    if (!status(
            scf::encodeData(
                outgoing,
                forwarding::DESTINATION,
                frame,
                sizeof(frame),
                length),
            "SCF Encoding",
            "DATA ENCODING FAILED"))
        return;

    printIdentity(outgoing);

    Serial.printf(
        "Payload Length: %u bytes\n"
        "Destination   : %lu\n"
        "Encoded Frame : %u bytes\n"
        "Remaining TTL : %lu ms\n",
        outgoing.payloadLength,
        static_cast<unsigned long>(forwarding::DESTINATION),
        static_cast<unsigned>(length),
        static_cast<unsigned long>(outgoing.expiresAt)
    );

    forwarding::hex(outgoing.payload, outgoing.payloadLength);

    Serial.println("\nLOCAL TRANSMISSION\n------------------");

    if (!status(
            LoRa.beginPacket() == 1,
            "TX Open",
            "RADIO DID NOT OPEN PACKET"))
        return;

    if (!status(
            LoRa.write(frame, length) == length,
            "TX Write",
            "INCOMPLETE DATA WRITE"))
    {
        LoRa.idle();
        return;
    }

    status(
        LoRa.endPacket() == 1,
        "TX Completion",
        "LOCAL TRANSMISSION DID NOT COMPLETE"
    );

    Serial.println(
        "Message retained in buffer. "
        "Node C reception requires receiver evidence."
    );
}

} // namespace

void setup()
{
    ready = forwarding::initialize();

    Serial.println();
    Serial.println(SEPARATOR);
    Serial.println("             NODE B - SCF DIAGNOSTIC");
    Serial.println(SEPARATOR);

    Serial.println(
        "LoRa packet classification -> Beacon processing "
        "or RAW display"
    );

    Serial.printf(
        "Radio Ready   : %s\n",
        ready ? "PASS" : "FAIL"
    );

    Serial.println(
        "Command       : f = forward an eligible stored message"
    );

    Serial.println("Beacon and RAW packets <=64 bytes are SCF eligible.");
    Serial.println("Press f for manual forwarding; no automatic forwarding.");

    Serial.println(
        "No ACK/retry/scheduler. "
        "Synthetic IDs do not suppress raw Beacon duplicates."
    );

    Serial.println(SEPARATOR);
    Serial.println();
    Serial.println();
}

void loop()
{
    if (!ready)
        return;

    buffer.expire(millis());

    if (Serial.available() && Serial.read() == 'f')
        forward();

    // Maximum SX127x LoRa payload: 255 bytes.
    uint8_t bytes[255];

    size_t length = 0;
    bool complete = false;

    if (!receivePacket(bytes, sizeof(bytes), length, complete))
        return;

    ++receivedPackets;

    const int rssi = LoRa.packetRssi();
    const float snr = LoRa.packetSnr();

    beacon::Packet beaconPacket = {};

    // A packet is a Beacon only when its size and existing
    // Beacon decoder validation both match.
    const bool validBeacon =
        complete &&
        length == beacon::PACKET_SIZE &&
        beacon::decode(
            bytes,
            length,
            beaconPacket
        ) == beacon::Result::Ok;

    // Do not wrap a production SCF DATA frame as a new raw message.
    // Multi-hop relay needs decode/identity-preserving handling (not implemented here).
    if (complete && length >= scf::FRAME_HEADER_SIZE &&
        bytes[0] == scf::FRAME_MAGIC && bytes[2] == scf::DATA_FRAME_TYPE)
    {
        printRawPacket(bytes, length, complete, rssi, snr);
        Serial.println("SCF FRAME: relay not implemented; NOT stored or forwarded");
        return;
    }

    if (validBeacon)
    {
        printHeader("BEACON RECEIVED", length, rssi, snr);
        Serial.println("\nORIGINAL PAYLOAD\n----------------");
        forwarding::hex(bytes, length);
        Serial.printf("\nBeacon MAC    : %02X:%02X:%02X:%02X:%02X:%02X\nSequence      : %u\n",
                      beaconPacket.nodeId[0], beaconPacket.nodeId[1], beaconPacket.nodeId[2],
                      beaconPacket.nodeId[3], beaconPacket.nodeId[4], beaconPacket.nodeId[5],
                      beaconPacket.sequence);
        Serial.println("\nGPS INFORMATION\n---------------");
        Serial.printf("Latitude      : %.7f\nLongitude     : %.7f\nSpeed         : %u cm/s\nHeading       : %u degrees\nTimestamp     : %lu%s\n",
                      beaconPacket.latitude / 10000000.0, beaconPacket.longitude / 10000000.0,
                      beaconPacket.speed, beaconPacket.heading,
                      static_cast<unsigned long>(beaconPacket.timestamp),
                      beaconPacket.timestamp ? " Unix seconds" : " (unsynchronized)");
    }
    else
    {
        printRawPacket(bytes, length, complete, rssi, snr);
    }

    // Both Beacon and RAW packets use the same SCF storage path.
    // Invalid/oversized/empty packets cannot be safely stored.
    if (!complete || length == 0 || length > scf::MAX_PAYLOAD_SIZE)
    {
        Serial.println("SCF STORAGE: REJECTED - incomplete, empty, or payload exceeds 64 bytes");
        return;
    }

    Serial.println("\nSCF PROCESSING\n--------------");
    Serial.println(validBeacon ? "Packet Type   : BEACON" : "Packet Type   : RAW / UNKNOWN");
    ReportFooter footer;

    const uint32_t now = millis();

    if (!status(
            now <= UINT32_MAX - forwarding::TTL &&
            sequence != UINT32_MAX,
            "Time/Sequence",
            "TIMESTAMP OR SEQUENCE WOULD WRAP"))
        return;

    scf::Message message = {};

    message.version = scf::MODEL_VERSION;
    message.bundleId = {
        forwarding::ORIGIN,
        ++sequence
    };
    message.createdAt = now;
    message.expiresAt = now + forwarding::TTL;
    message.priority = scf::Priority::Normal;
    message.hopLimit = 4;

    if (!status(
            scf::setPayload(message, bytes, length) &&
            scf::isValidMessage(message),
            "SCF Wrapping",
            "INVALID MESSAGE"))
        return;

    printIdentity(message);

    const bool duplicate = history.contains(message.bundleId);

    if (!status(
            !duplicate &&
            history.count() < scf::DuplicateTracker::CAPACITY,
            "Identity",
            duplicate
                ? "DUPLICATE IDENTITY"
                : "HISTORY FULL"))
        return;

    const auto admission = buffer.enqueue(message);

    if (!status(
            admission == scf::Buffer::EnqueueResult::Enqueued,
            "Buffer Store",
            bufferFailure(admission)))
        return;

    const auto remembered = history.remember(message.bundleId);

    if (!status(
            remembered ==
                scf::DuplicateTracker::RememberResult::Added,
            "History Store",
            historyFailure(remembered)))
    {
        buffer.remove(message.bundleId);
        return;
    }

    const scf::Message *stored =
        buffer.find(message.bundleId);

    status(
        stored &&
        stored->payloadLength == length &&
        memcmp(stored->payload, bytes, length) == 0,
        "Payload Check",
        "STORED PAYLOAD MISMATCH OR LOOKUP FAILED"
    );
}
