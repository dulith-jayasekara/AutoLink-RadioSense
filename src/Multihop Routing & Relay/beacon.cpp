#include "beacon.h"
#include "Project_Headers.h"

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

        Serial.print("  Latitude: ");
        if (packet.latitude == GPS_LATITUDE_UNAVAILABLE)
            Serial.println("unavailable");
        else
            Serial.println(static_cast<double>(packet.latitude) / 10000000.0, 7);

        Serial.print("  Longitude: ");
        if (packet.longitude == GPS_LONGITUDE_UNAVAILABLE)
            Serial.println("unavailable");
        else
            Serial.println(static_cast<double>(packet.longitude) / 10000000.0, 7);

        Serial.print("  Speed: ");
        if (packet.speed == GPS_SPEED_UNAVAILABLE)
            Serial.println("unavailable");
        else
        {
            Serial.print(packet.speed);
            Serial.println(" cm/s");
        }

        Serial.print("  Heading: ");
        if (packet.heading == GPS_HEADING_UNAVAILABLE)
        {
            Serial.println("unavailable");
        }
        else
        {
            Serial.print(packet.heading);
            Serial.println(" degrees");
        }

        Serial.print("  Timestamp (Unix seconds): ");
        if (packet.timestamp == GPS_TIMESTAMP_UNAVAILABLE)
            Serial.println("unavailable");
        else
            Serial.println(packet.timestamp);
    }

    void MarkGpsFieldsUnavailable(BeaconPacket &packet)
    {
        packet.latitude = GPS_LATITUDE_UNAVAILABLE;
        packet.longitude = GPS_LONGITUDE_UNAVAILABLE;
        packet.speed = GPS_SPEED_UNAVAILABLE;
        packet.heading = GPS_HEADING_UNAVAILABLE;
        packet.timestamp = GPS_TIMESTAMP_UNAVAILABLE;
    }

    void SetFreshnessUnavailable(BeaconGPSFreshnessInfo &freshnessInfo)
    {
        freshnessInfo.positionState =
            static_cast<uint8_t>(GPS_UNAVAILABLE);
        freshnessInfo.speedState =
            static_cast<uint8_t>(GPS_UNAVAILABLE);
        freshnessInfo.headingState =
            static_cast<uint8_t>(GPS_UNAVAILABLE);
        freshnessInfo.measurementAgeAvailable = false;
    }

    bool validateBeaconNodeID(const uint8_t *nodeID)
    {
        if (nodeID == nullptr)
        {
            Serial.println("Ignored beacon with a missing Node ID.");
            return false;
        }

        uint8_t ownNodeID[BEACON_MAC_SIZE] = {};
        WiFi.macAddress(ownNodeID);
        if (memcmp(nodeID, ownNodeID, sizeof(ownNodeID)) == 0)
        {
            Serial.println("Ignored beacon from this node.");
            return false;
        }

        // All-zero and broadcast addresses are not individual node IDs.
        bool allZero = true;
        bool allBroadcast = true;
        for (uint8_t index = 0; index < BEACON_MAC_SIZE; ++index)
        {
            allZero = allZero && nodeID[index] == 0x00;
            allBroadcast = allBroadcast && nodeID[index] == 0xFF;
        }
        if (allZero)
        {
            Serial.println("Ignored beacon with an all-zero Node ID.");
            return false;
        }
        if (allBroadcast)
        {
            Serial.println("Ignored beacon with a broadcast Node ID.");
            return false;
        }

        // The least-significant bit of the first octet is the Individual/
        // Group bit; locally administered unicast addresses remain valid.
        if ((nodeID[0] & 0x01) != 0)
        {
            Serial.println("Ignored beacon with a multicast Node ID.");
            return false;
        }

        return true;
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

// Populate every GPS field from one coherently evaluated phone snapshot.
void generateBeacon(BeaconPacket &packet)
{
    packet.packetType = BEACON_PACKET_TYPE;
    WiFi.macAddress(packet.nodeID);

    packet.latitude = GPS_LATITUDE_UNAVAILABLE;
    packet.longitude = GPS_LONGITUDE_UNAVAILABLE;
    packet.speed = GPS_SPEED_UNAVAILABLE;
    packet.heading = GPS_HEADING_UNAVAILABLE;
    packet.timestamp = GPS_TIMESTAMP_UNAVAILABLE;

    GPSMeasurementSnapshot snapshot = {};
    if (GPS_GetSnapshot(snapshot))
    {
        if (snapshot.positionAvailable)
        {
            packet.latitude =
                static_cast<int32_t>(snapshot.latitude * 10000000.0);
            packet.longitude =
                static_cast<int32_t>(snapshot.longitude * 10000000.0);
        }
        if (snapshot.speedAvailable)
        {
            const double encodedSpeed =
                snapshot.speedMetersPerSecond * 100.0;
            if (isfinite(encodedSpeed) &&
                encodedSpeed >= 0.0 &&
                encodedSpeed < static_cast<double>(GPS_SPEED_UNAVAILABLE))
            {
                packet.speed =
                    static_cast<uint16_t>(encodedSpeed + 0.5);
            }
        }
        if (snapshot.headingAvailable)
        {
            const uint16_t roundedHeading =
                static_cast<uint16_t>(snapshot.headingDegrees + 0.5);
            packet.heading = roundedHeading == 360 ? 0 : roundedHeading;
        }
        if (snapshot.timestampAvailable)
        {
            const uint64_t timestampSeconds =
                snapshot.fixTimestampMilliseconds / 1000ULL;
            if (timestampSeconds < GPS_TIMESTAMP_UNAVAILABLE)
            {
                packet.timestamp =
                    static_cast<uint32_t>(timestampSeconds);
            }
        }
    }
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

    Serial.println();
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

// BEACON PACKET AND GPS VALIDATION
// Validate a decoded packet before it is passed to Neighbour_Table. Invalid
// packet identity or sequence rejects the beacon; invalid GPS fields are
// replaced with their on-wire unavailable sentinels so link metrics survive.
bool validateReceivedBeacon(
    BeaconPacket &packet,
    uint16_t &sequenceDifference,
    BeaconGPSUpdate &gpsUpdate,
    BeaconGPSFreshnessInfo &freshnessInfo)
{
    sequenceDifference = 0;
    gpsUpdate = BeaconGPSUpdate::Invalidate;
    SetFreshnessUnavailable(freshnessInfo);

    // Reject invalid node identities before reading or updating neighbour state.
    if (!validateBeaconNodeID(packet.nodeID))
    {
        return false;
    }

    uint16_t previousSequence = 0;
    uint32_t previousGpsTimestamp = GPS_TIMESTAMP_UNAVAILABLE;
    const bool knownNeighbour = getNeighbourValidationHistory(
        packet.nodeID,
        previousSequence,
        previousGpsTimestamp);

    if (knownNeighbour)
    {
        // Unsigned 16-bit subtraction preserves the existing rollover rule:
        // accept forward differences below half the sequence-number space.
        sequenceDifference =
            static_cast<uint16_t>(packet.sequenceNumber - previousSequence);
        if (sequenceDifference == 0 || sequenceDifference >= 32768)
        {
            Serial.println(
                sequenceDifference == 0
                    ? "Ignored duplicate beacon sequence."
                    : "Ignored out-of-order beacon sequence.");
            return false;
        }
    }

    // Missing timestamps cannot be ordered or freshness-checked, so preserve
    // the established behavior of invalidating this packet's GPS fields only.
    if (packet.timestamp == GPS_TIMESTAMP_UNAVAILABLE ||
        packet.timestamp == 0)
    {
        MarkGpsFieldsUnavailable(packet);
        gpsUpdate = BeaconGPSUpdate::Invalidate;
        return true;
    }

    // The packet stores whole Unix seconds; widen before multiplying so the
    // conversion to the Core validators' millisecond units cannot overflow.
    const uint64_t fixTimestampMilliseconds =
        static_cast<uint64_t>(packet.timestamp) * 1000ULL;
    if (!GPS_ValidateFixTimestamp(fixTimestampMilliseconds))
    {
        Serial.println("Beacon GPS timestamp is invalid; retaining link data only.");
        MarkGpsFieldsUnavailable(packet);
        gpsUpdate = BeaconGPSUpdate::Invalidate;
        return true;
    }

    if (knownNeighbour &&
        previousGpsTimestamp != GPS_TIMESTAMP_UNAVAILABLE &&
        packet.timestamp < previousGpsTimestamp)
    {
        // The link packet is still useful, but older GPS measurements must
        // not replace the neighbour's newer location or motion values.
        getNeighbourGPSFreshnessInfo(packet.nodeID, freshnessInfo);
        gpsUpdate = BeaconGPSUpdate::Preserve;
        Serial.println("Beacon GPS timestamp is older than the stored measurement; preserving neighbour GPS data.");
        return true;
    }

    gpsUpdate = knownNeighbour &&
        packet.timestamp == previousGpsTimestamp
        ? BeaconGPSUpdate::MergeUnavailable
        : BeaconGPSUpdate::Replace;

    if (packet.latitude == GPS_LATITUDE_UNAVAILABLE ||
        packet.longitude == GPS_LONGITUDE_UNAVAILABLE)
    {
        packet.latitude = GPS_LATITUDE_UNAVAILABLE;
        packet.longitude = GPS_LONGITUDE_UNAVAILABLE;
    }
    else
    {
        // Decode the packet's degrees-times-10^7 integer representation.
        const double latitude =
            static_cast<double>(packet.latitude) / 10000000.0;
        const double longitude =
            static_cast<double>(packet.longitude) / 10000000.0;
        if (!GPS_ValidatePosition(latitude, longitude))
        {
            Serial.println("Beacon position is invalid; marking coordinates unavailable.");
            packet.latitude = GPS_LATITUDE_UNAVAILABLE;
            packet.longitude = GPS_LONGITUDE_UNAVAILABLE;
        }
    }

    double speedMetersPerSecond = 0.0;
    bool speedAvailable = packet.speed != GPS_SPEED_UNAVAILABLE;
    if (speedAvailable)
    {
        // GPS_ValidateSpeed uses metres per second; the packet uses cm/s.
        speedMetersPerSecond =
            static_cast<double>(packet.speed) / 100.0;
        bool speedWasClamped = false;
        if (!GPS_ValidateSpeed(speedMetersPerSecond, speedWasClamped))
        {
            packet.speed = GPS_SPEED_UNAVAILABLE;
            speedAvailable = false;
        }
        else if (speedWasClamped)
        {
            // The Core validator clamps to its configured maximum. Truncate
            // the converted cm/s value so the integer packet stays at or
            // below that maximum.
            packet.speed = static_cast<uint16_t>(
                speedMetersPerSecond * 100.0);
            Serial.println("Beacon speed exceeded the configured limit and was clamped.");
        }
    }

    // Heading is already represented in degrees in the packet.
    if (packet.heading != GPS_HEADING_UNAVAILABLE &&
        !GPS_ValidateHeading(static_cast<double>(packet.heading)))
    {
        Serial.println("Beacon heading is invalid; marking it unavailable.");
        packet.heading = GPS_HEADING_UNAVAILABLE;
    }

    bool measurementAgeAvailable = false;
    uint64_t measurementAgeMilliseconds = 0;
    if (Time_IsSynchronized())
    {
        uint64_t currentTimestampMilliseconds = 0;
        if (Time_GetCurrentTimestampMilliseconds(
                currentTimestampMilliseconds))
        {
            measurementAgeAvailable = true;
            freshnessInfo.measurementAgeAvailable = true;
            if (fixTimestampMilliseconds > currentTimestampMilliseconds)
            {
                const uint64_t futureOffsetMilliseconds =
                    fixTimestampMilliseconds - currentTimestampMilliseconds;
                if (futureOffsetMilliseconds >
                    GPS_FUTURE_TIMESTAMP_TOLERANCE_MS)
                {
                    Serial.println("Beacon GPS timestamp is too far in the future; retaining link data only.");
                    MarkGpsFieldsUnavailable(packet);
                    gpsUpdate = BeaconGPSUpdate::Invalidate;
                    return true;
                }

                measurementAgeMilliseconds =
                    futureOffsetMilliseconds +
                    GPS_TIMESTAMP_AGE_UNCERTAINTY_MS;
            }
            else
            {
                measurementAgeMilliseconds =
                    currentTimestampMilliseconds -
                    fixTimestampMilliseconds +
                    GPS_TIMESTAMP_AGE_UNCERTAINTY_MS;
            }
        }
        else
        {
            Serial.println("Local clock cannot provide GPS age; skipping beacon age checks.");
        }
    }
    // Without a synchronized local clock, absolute age cannot be measured.
    // Keep otherwise valid GPS fields and continue packet/history validation.

    if (measurementAgeAvailable)
    {
        const double positionSpeedMetersPerSecond = speedAvailable
            ? speedMetersPerSecond
            : GPS_POSITION_FALLBACK_SPEED_MPS;
        const uint32_t positionFreshnessThreshold =
            GPS_CalculatePositionFreshnessThresholdMs(
                positionSpeedMetersPerSecond);
        const uint32_t positionExpirationThreshold =
            GPS_CalculatePositionExpirationThresholdMs(
                positionSpeedMetersPerSecond);

        const GPS_FreshnessState positionState =
            GPS_DetermineFreshnessState(
            packet.latitude != GPS_LATITUDE_UNAVAILABLE &&
                packet.longitude != GPS_LONGITUDE_UNAVAILABLE,
            measurementAgeMilliseconds,
            positionFreshnessThreshold,
            positionExpirationThreshold);
        freshnessInfo.positionState =
            static_cast<uint8_t>(positionState);
        if (positionState == GPS_EXPIRED)
        {
            Serial.println("Beacon position has expired; marking coordinates unavailable.");
            packet.latitude = GPS_LATITUDE_UNAVAILABLE;
            packet.longitude = GPS_LONGITUDE_UNAVAILABLE;
        }

        const uint32_t speedExpirationThreshold =
            speedMetersPerSecond == GPS_MIN_SPEED_MPS
            ? GPS_ZERO_SPEED_EXPIRE_MS
            : GPS_SPEED_EXPIRE_MS;
        const GPS_FreshnessState speedState =
            GPS_DetermineFreshnessState(
            speedAvailable,
            measurementAgeMilliseconds,
            GPS_SPEED_FRESH_MS,
            speedExpirationThreshold);
        freshnessInfo.speedState = static_cast<uint8_t>(speedState);
        if (speedState == GPS_EXPIRED)
        {
            Serial.println("Beacon speed has expired; marking it unavailable.");
            packet.speed = GPS_SPEED_UNAVAILABLE;
        }

        const GPS_FreshnessState headingState =
            GPS_DetermineFreshnessState(
            packet.heading != GPS_HEADING_UNAVAILABLE,
            measurementAgeMilliseconds,
            GPS_HEADING_FRESH_MS,
            GPS_HEADING_EXPIRE_MS);
        freshnessInfo.headingState =
            static_cast<uint8_t>(headingState);
        if (headingState == GPS_EXPIRED)
        {
            Serial.println("Beacon heading has expired; marking it unavailable.");
            packet.heading = GPS_HEADING_UNAVAILABLE;
        }
    }
    else
    {
        // Without a verifiable age, GPS_UNAVAILABLE means "freshness unknown"
        // here; the valid packet fields are retained and are not called fresh.
        Serial.println("Beacon GPS freshness is unverified; retaining valid fields.");
    }

    return true;
}

// Display the information extracted into BeaconPacket by receiveBeacon().
void extractBeaconInformation(const BeaconPacket &packet)
{
    Serial.println("Received beacon:");
    printBeacon(packet);
}


// -------------------------------------------------------------
// ADAPTIVE BEACON INTERVAL CALCULATION
// -------------------------------------------------------------

uint32_t calculateBeaconInterval(uint16_t speed,bool speedAvailable){
    // Use the default interval if vehicle speed is unavailable.
    if (!speedAvailable || speed == GPS_SPEED_UNAVAILABLE)
    {
        return BEACON_DEFAULT_INTERVAL_MS;
    }

    // Convert speed from centimetres per second to kilometres per hour.
    const float speedKmph = speed * 0.036f;

    // If the vehicle is stationary, use the maximum interval.
    if (speedKmph <= BEACON_STATIONARY_SPEED_KMPH)
    {
        return BEACON_MAX_INTERVAL_MS;
    }

    // If the vehicle reaches or exceeds the reference maximum speed, use the minimum interval.
    if (speedKmph >= BEACON_MAX_SPEED_KMPH)
    {
        return BEACON_MIN_INTERVAL_MS;
    }

    // Calculate the linear interval reduction coefficient.
    const float k =
        static_cast<float>(
            BEACON_MAX_INTERVAL_MS - BEACON_MIN_INTERVAL_MS
        ) / BEACON_MAX_SPEED_KMPH;

    // Calculate the adaptive beacon interval.
    const float interval = BEACON_MAX_INTERVAL_MS - (k * speedKmph);

    // Convert the calculated interval into milliseconds.
    return static_cast<uint32_t>(interval + 0.5f);
}

void handleBeaconTransmission(
    uint16_t speed,
    bool speedAvailable)
{
    static BeaconPacket outgoingBeacon = {};
    static uint32_t lastBeaconTransmit = 0;

    const uint32_t now = millis();
    const uint32_t interval = calculateBeaconInterval(speed, speedAvailable);

    if (now - lastBeaconTransmit >= interval)
    {
        generateBeacon(outgoingBeacon);

        outgoingBeacon.speed =
            speedAvailable && speed != GPS_SPEED_UNAVAILABLE
            ? speed
            : GPS_SPEED_UNAVAILABLE;

        Serial.println("Adaptive Beacon Transmission");
        if (speedAvailable && speed != GPS_SPEED_UNAVAILABLE)
        {
            const float speedKmph = speed * 0.036f;
            Serial.print("Vehicle speed: ");
            Serial.print(speedKmph, 2);
            Serial.println(" km/h");
        }
        else
        {
            Serial.println("Vehicle speed: unavailable");
        }
        Serial.print("Beacon interval: ");
        Serial.print(interval);
        Serial.println(" ms");
        Serial.println();

        transmitBeacon(outgoingBeacon);
        lastBeaconTransmit = now;
    }
}
