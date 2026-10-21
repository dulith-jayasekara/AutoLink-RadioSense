#include "beacon.h"
#include "../Core/Time_Manager.h"
#include "../Core/Captive_Portal.h"

// Anonymous namespace:
// These variables and helper functions are private to this .cpp file.
namespace
{
    // Stores the sequence number used for outgoing beacons.
    uint16_t sequenceNumber = 0;

    // ---------------------------------------------------------
    // ENCODING HELPERS
    // Convert integer values into individual bytes.
    // ---------------------------------------------------------

    // Writes a 16-bit unsigned integer into the buffer.
    //
    // Little-endian format:
    // The least-significant byte is written first.
    //
    // Example:
    // value = 0x1234
    // Buffer receives: 0x34, 0x12
    //
    // offset identifies the next available buffer position.
    // It is passed by reference so it advances for the caller too.
    void writeUint16(uint8_t *buffer, size_t &offset, uint16_t value)
    {
        buffer[offset++] = static_cast<uint8_t>(value);
        buffer[offset++] = static_cast<uint8_t>(value >> 8);
    }


    // Writes a 32-bit unsigned integer as four bytes.
    //
    // Example:
    // value = 0x12345678
    // Buffer receives: 0x78, 0x56, 0x34, 0x12
    //
    // Each right shift moves the next byte into the lowest 8 bits.
    void writeUint32(uint8_t *buffer, size_t &offset, uint32_t value)
    {
        for (uint8_t byte = 0; byte < 4; ++byte)
        {
            buffer[offset++] =
                static_cast<uint8_t>(value >> (byte * 8));
        }
    }


    // ---------------------------------------------------------
    // DECODING HELPERS
    // Reconstruct integers from bytes received over LoRa.
    // ---------------------------------------------------------

    // Reads two bytes and reconstructs a 16-bit unsigned integer.
    //
    // This expects little-endian data, matching writeUint16().
    // The bitwise OR combines the two bytes into one value.
    uint16_t readUint16(const uint8_t *buffer, size_t &offset)
    {
        const uint16_t value =
            static_cast<uint16_t>(buffer[offset]) |
            (static_cast<uint16_t>(buffer[offset + 1]) << 8);

        offset += 2;
        return value;
    }


    // Reads four bytes and reconstructs a 32-bit unsigned integer.
    //
    // Each byte is shifted into its correct position and combined
    // with the other bytes using bitwise OR.
    uint32_t readUint32(const uint8_t *buffer, size_t &offset)
    {
        uint32_t value = 0;

        for (uint8_t byte = 0; byte < 4; ++byte)
        {
            value |=
                static_cast<uint32_t>(buffer[offset++])
                << (byte * 8);
        }

        return value;
    }


    // ---------------------------------------------------------
    // SERIALIZATION
    // Convert the BeaconPacket structure into a byte array.
    // ---------------------------------------------------------

    // The structure is convenient for our application code,
    // but it must not be transmitted directly as raw memory.
    //
    // A compiler may insert padding bytes into a structure.
    // Different processors may also represent multi-byte values
    // differently in memory.
    //
    // Instead, this function writes every field explicitly in
    // the agreed packet order.
    void serializeBeacon(const BeaconPacket &packet, uint8_t *buffer)
    {
        // Begin writing at the first position in the buffer.
        size_t offset = 0;

        // Field 1: Packet Type (1 byte).
        buffer[offset++] = packet.packetType;

        // Field 2: Node ID (6 bytes).
        // Copy each MAC-address byte into the payload.
        for (uint8_t byte : packet.nodeID)
        {
            buffer[offset++] = byte;
        }

        // Field 3: Sequence Number (2 bytes).
        writeUint16(buffer, offset, packet.sequenceNumber);

        // Field 4: Latitude (4 bytes).
        // The latitude is stored as degrees multiplied by 10^7.
        // The conversion preserves its underlying 32-bit bit pattern.
        writeUint32(
            buffer, offset,
            static_cast<uint32_t>(packet.latitude)
        );

        // Field 5: Longitude (4 bytes).
        // Longitude uses the same degrees * 10^7 representation.
        writeUint32(
            buffer, offset,
            static_cast<uint32_t>(packet.longitude)
        );

        // Field 6: Speed (2 bytes), measured in centimetres per second.
        writeUint16(buffer, offset, packet.speed);

        // Field 7: Heading (2 bytes).
        // This code currently stores the heading as a whole number
        // of degrees, according to the current structure comment.
        writeUint16(buffer, offset, packet.heading);

        // Field 8: Timestamp (4 bytes), Unix time in seconds.
        writeUint32(buffer, offset, packet.timestamp);
    }


    // ---------------------------------------------------------
    // DESERIALIZATION
    // Convert a received byte array back into BeaconPacket.
    // ---------------------------------------------------------

    // This performs the reverse of serializeBeacon().
    //
    // The field order and byte order must match the transmitter.
    // Otherwise, the receiver may decode incorrect values.
    void deserializeBeacon(const uint8_t *buffer, BeaconPacket &packet)
    {
        size_t offset = 0;

        // Field 1: Read Packet Type.
        packet.packetType = buffer[offset++];

        // Field 2: Read the six Node ID bytes.
        for (uint8_t &byte : packet.nodeID)
        {
            byte = buffer[offset++];
        }

        // Field 3: Read Sequence Number.
        packet.sequenceNumber = readUint16(buffer, offset);

        // Field 4: Read Latitude.
        // Convert the reconstructed 32-bit value back to int32_t
        // because latitude may be negative.
        packet.latitude =
            static_cast<int32_t>(readUint32(buffer, offset));

        // Field 5: Read Longitude.
        // Longitude may also be negative.
        packet.longitude =
            static_cast<int32_t>(readUint32(buffer, offset));

        // Field 6: Read Speed.
        packet.speed = readUint16(buffer, offset);

        // Field 7: Read Heading.
        packet.heading = readUint16(buffer, offset);

        // Field 8: Read Timestamp.
        packet.timestamp = readUint32(buffer, offset);
    }


    // ---------------------------------------------------------
    // DEBUG OUTPUT
    // Display the decoded or outgoing beacon in Serial Monitor.
    // ---------------------------------------------------------

    void printBeacon(const BeaconPacket &packet)
    {
        // Print the Node ID in hexadecimal MAC-address format.
        Serial.print("  Node ID: ");

        for (uint8_t byte : packet.nodeID)
        {
            // Add a leading zero to single-digit hexadecimal bytes.
            // For example, print 0A rather than A.
            if (byte < 0x10)
            {
                Serial.print('0');
            }

            Serial.print(byte, HEX);
            Serial.print(':');
        }
        Serial.println();

        // Display the beacon sequence number.
        Serial.print("  Sequence: ");
        Serial.println(packet.sequenceNumber);

        // Convert the stored latitude back to decimal degrees.
        Serial.print("  Latitude: ");
        Serial.println(
            static_cast<double>(packet.latitude) / 10000000.0,
            7
        );

        // Convert the stored longitude back to decimal degrees.
        Serial.print("  Longitude: ");
        Serial.println(
            static_cast<double>(packet.longitude) / 10000000.0,
            7
        );

        // Speed is already stored in centimetres per second.
        Serial.print("  Speed: ");
        Serial.print(packet.speed);
        Serial.println(" cm/s");

        // Print heading using the units defined by the packet format.
        Serial.print("  Heading: ");
        Serial.print(packet.heading);
        Serial.println(" degrees");

        // The packet stores Unix time in seconds; zero means the sender's
        // clock had not been synchronized when this beacon was created.
        Serial.print("  Timestamp (Unix seconds): ");
        if (packet.timestamp == 0)
        {
            Serial.println("not synchronized (0)");
        }
        else
        {
            Serial.println(packet.timestamp);
        }
    }
}


// -------------------------------------------------------------
// SEQUENCE NUMBER GENERATION
// -------------------------------------------------------------

uint16_t generateSequenceNumber()
{
    sequenceNumber++;
    return sequenceNumber;
}

// Populate the fields currently available on this device. Sensor-derived
// location, speed, heading, and time can be added here when those sources exist.
void generateBeacon(BeaconPacket &packet)
{
    packet.packetType = BEACON_PACKET_TYPE;
    WiFi.macAddress(packet.nodeID);
    double latitude = 0.0;
    double longitude = 0.0;
    if (CaptivePortal_GetLocation(latitude, longitude))
    {
        packet.latitude = static_cast<int32_t>(latitude * 10000000.0);
        packet.longitude = static_cast<int32_t>(longitude * 10000000.0);
        Serial.print("Using phone location for beacon: ");
        Serial.print(latitude, 7);
        Serial.print(", ");
        Serial.println(longitude, 7);
        Serial.print("\n\n");
    }
    else
    {
        packet.latitude = 0;
        packet.longitude = 0;
        Serial.println("No phone location available; beacon coordinates are zero.");
    }
    packet.speed = 0;
    packet.heading = 0;
    // Keep using the packet's existing four-byte seconds field. Zero is the
    // established invalid marker while this node waits for phone time.
    packet.timestamp = Time_GetCurrentTimestamp();
}


// -------------------------------------------------------------
// BEACON TRANSMISSION
// -------------------------------------------------------------

// Prepares a beacon and sends it through the LoRa radio.
//
// Returns:
// true  = transmission operation reported success
// false = an error occurred during the transmission operation
bool transmitBeacon(BeaconPacket &packet)
{
    // Ensure this packet is identified as a Beacon.
    packet.packetType = BEACON_PACKET_TYPE;

    // Assign a fresh sequence number for this transmission.
    packet.sequenceNumber = generateSequenceNumber();

    // Allocate a byte array for the serialized packet.
    // Its size must match the packet format defined in beacon.h.
    uint8_t payload[BEACON_PACKET_SIZE];

    // Convert the structure into bytes before sending.
    serializeBeacon(packet, payload);

    // Print the values for debugging.
    Serial.println("Transmitting beacon:");
    printBeacon(packet);

    // Ask the LoRa library to start a new outgoing packet.
    // A return value of 0 indicates failure.
    if (LoRa.beginPacket() == 0)
    {
        Serial.println(
            "Beacon transmission failed: could not start packet."
        );
        return false;
    }

    // Write the serialized bytes into the LoRa packet.
    // bytesWritten tells us how many bytes were accepted.
    const size_t bytesWritten = LoRa.write(payload, sizeof(payload));

    // Finish the packet and initiate transmission.
    // With the standard LoRa library in blocking mode,
    // a result of 1 indicates success.
    const int result = LoRa.endPacket();

    // Check that the complete payload was written and the
    // library reported successful transmission.
    if (bytesWritten != sizeof(payload) || result != 1)
    {
        Serial.println("Beacon transmission failed.");
        return false;
    }

    Serial.println("Beacon transmitted successfully.");
    return true;
}


// -------------------------------------------------------------
// BEACON RECEPTION
// -------------------------------------------------------------

// Checks for an incoming LoRa packet, validates it, and decodes it.
//
// Returns:
// true  = a valid beacon was decoded
// false = no packet was available or the packet was rejected
bool receiveBeacon(BeaconPacket &packet, BeaconReceptionInfo &receptionInfo)
{
    // Check whether a LoRa packet has arrived.
    // parsePacket() returns 0 when no packet is available.
    //
    // Otherwise, it returns the size of the received payload.
    const int packetSize = LoRa.parsePacket();

    if (packetSize == 0)
    {
        // Nothing to process during this function call.
        return false;
    }

    // Only accept packets with the expected beacon payload size.
    if (packetSize != BEACON_PACKET_SIZE)
    {
        // Read and discard any remaining bytes from the
        // unexpected packet.
        while (LoRa.available())
        {
            LoRa.read();
        }

        Serial.print("Ignored packet with invalid size: ");
        Serial.print(packetSize);
        Serial.println(" bytes.");

        return false;
    }

    // Temporary storage for the received packet bytes.
    uint8_t payload[BEACON_PACKET_SIZE];

    // Read every expected byte from the LoRa receive buffer.
    for (size_t index = 0; index < sizeof(payload); ++index)
    {
        // Verify that another byte is available before reading.
        if (!LoRa.available())
        {
            Serial.println("Ignored incomplete beacon packet.");
            return false;
        }

        payload[index] = static_cast<uint8_t>(LoRa.read());
    }

    // Convert the byte array into the BeaconPacket structure.
    deserializeBeacon(payload, packet);

    // Confirm that this packet is actually a Beacon.
    // A correctly sized packet might belong to another message type.
    if (packet.packetType != BEACON_PACKET_TYPE)
    {
        Serial.print("Ignored packet with unknown type: 0x");
        Serial.println(packet.packetType, HEX);
        return false;
    }

    // RSSI indicates the received signal strength.
    // A value closer to zero generally means a stronger signal.
    receptionInfo.rssi = static_cast<int16_t>(LoRa.packetRssi());
    receptionInfo.snr = LoRa.packetSnr();

    Serial.print("  RSSI: ");
    Serial.print(receptionInfo.rssi);
    Serial.println(" dBm");

    // SNR indicates the signal-to-noise ratio.
    // It provides additional information about reception quality.
    Serial.print("  SNR: ");
    Serial.print(receptionInfo.snr, 2);
    Serial.println(" dB");

    // The beacon passed the size and packet-type checks.
    return true;
}

// Display the information extracted into BeaconPacket by receiveBeacon().
void extractBeaconInformation(const BeaconPacket &packet)
{
    Serial.println("Received beacon:");
    printBeacon(packet);
}
