# AutoLink RadioSense
## Beacon Management Subsystem: Implementation and Technical Documentation

**Project:** AutoLink RadioSense  
**Subsystem:** Beacon Management  
**Hardware:** ESP32 DevKit with SX1278 Ra-02 LoRa module  
**Programming Environment:** PlatformIO, Arduino framework, C++  
**Primary Files:** `beacon.h` and `beacon.cpp`

---

## 1. Introduction

The Beacon Management subsystem is responsible for preparing, transmitting, receiving, and interpreting beacon packets in the AutoLink RadioSense vehicle-to-vehicle (V2V) communication system.

A beacon is a periodic message broadcast by a vehicle to communicate its identity and current status to nearby vehicles. The information can subsequently support neighbour discovery, link-quality assessment, and multi-hop relay selection.

In the AutoLink RadioSense system, the ESP32 acts as the main controller. It obtains vehicle information, prepares a beacon packet, and uses the SX1278 LoRa radio to transmit the packet to other vehicles.

The subsystem is divided into two primary source files:

- **`beacon.h`:** Defines the beacon packet structure, packet configuration constants, and public function declarations.
- **`beacon.cpp`:** Implements sequence number generation, packet encoding and decoding, beacon generation, LoRa transmission, LoRa reception, and beacon information extraction for debugging.

The implementation separates the application's representation of a beacon from its transmitted byte representation. This ensures that the packet follows a defined format instead of relying on the compiler's internal memory layout for C++ structures.

## 2. Objectives

The Beacon Management subsystem has the following objectives:

1. Define a consistent beacon packet format for all participating vehicles.
2. Generate beacon information using the vehicle's available data.
3. Assign a unique node identifier and a sequence number to outgoing beacons.
4. Encode the packet into a fixed-length byte array.
5. Transmit the encoded packet through the LoRa radio.
6. Receive and validate incoming beacon packets.
7. Decode received packets into a usable data structure.
8. Display beacon information and radio reception metrics for testing and debugging.

This subsystem focuses on beacon communication. Maintaining the neighbour table, selecting relay vehicles, and deciding forwarding routes are handled by other modules.

---

## 3. System Architecture

The beacon communication process consists of two main workflows.

### 3.1 Beacon transmission workflow

1. The ESP32 creates a `BeaconPacket` structure.
2. The beacon-generation function initializes the packet fields.
3. Vehicle information is populated into the packet.
4. A sequence number is assigned when transmission is initiated.
5. The packet is serialized into a byte array.
6. The LoRa library transmits the byte array through the SX1278 module.
7. The transmission result is checked and reported through the Serial Monitor.

### 3.2 Beacon reception workflow

1. The ESP32 checks whether a LoRa packet is available.
2. The received packet length is checked against the expected beacon size.
3. The payload bytes are read into a buffer.
4. The buffer is deserialized into a `BeaconPacket` structure.
5. The packet type is validated.
6. RSSI and SNR values are obtained from the LoRa library.
7. The decoded beacon information can be extracted and displayed for inspection.

### 3.3 Functional flow

```text
TRANSMISSION

Vehicle Data
     |
     v
generateBeacon()
     |
     v
Assign Sequence Number
     |
     v
serializeBeacon()
     |
     v
transmitBeacon()
     |
     v
SX1278 LoRa Transmission
     |
     v
Another Vehicle


RECEPTION

SX1278 LoRa Reception
     |
     v
receiveBeacon()
     |
     v
Validate Packet Length
     |
     v
deserializeBeacon()
     |
     v
Validate Packet Type
     |
     v
Extract Beacon Information
     |
     v
Neighbour Management (later integration)
```

The flow illustrates the intended processing sequence. In the current implementation, `generateBeacon()` and `transmitBeacon()` are separate functions, so the calling application must invoke them in the correct order.

---

## 4. Beacon Packet Definition

### 4.1 Packet structure

The beacon packet is represented in C++ by the `BeaconPacket` structure.

```cpp
struct BeaconPacket {
    uint8_t packetType;
    uint8_t nodeID[6];
    uint16_t sequenceNumber;
    int32_t latitude;
    int32_t longitude;
    uint16_t speed;
    uint16_t heading;
    uint32_t timestamp;
};
```

The structure contains eight fields. The combined size of their defined fields is 25 bytes.

### 4.2 Packet field specification

| Field | Data type | Size | Description |
|---|---|---:|---|
| Packet Type | `uint8_t` | 1 byte | Identifies the message as a beacon. Value: `0x01`. |
| Node ID | `uint8_t[6]` | 6 bytes | Six-byte ESP32 Wi-Fi MAC address used as the node identifier. |
| Sequence Number | `uint16_t` | 2 bytes | Identifies the order of outgoing beacons. |
| Latitude | `int32_t` | 4 bytes | Latitude in decimal degrees multiplied by \(10^7\). |
| Longitude | `int32_t` | 4 bytes | Longitude in decimal degrees multiplied by \(10^7\). |
| Speed | `uint16_t` | 2 bytes | Vehicle speed in centimetres per second (cm/s). |
| Heading | `uint16_t` | 2 bytes | Vehicle heading in whole degrees, according to the current implementation. |
| Timestamp | `uint32_t` | 4 bytes | Unix timestamp in seconds. |

**Total payload size:**

\[
1+6+2+4+4+2+2+4=25\text{ bytes}
\]

The `BEACON_PACKET_SIZE` constant must remain consistent with this packet definition.

### 4.3 Packet type

The packet type identifies the purpose of the transmitted message.

```cpp
constexpr uint8_t BEACON_PACKET_TYPE = 0x01;
```

The value `0x01` is reserved for beacon packets in the current implementation.

During reception, the decoded packet type is compared against this constant. Packets with a different type are rejected by the beacon receiver.

This allows the system to distinguish beacon packets from other message types that may be introduced in the future.

### 4.4 Node identifier

```cpp
uint8_t nodeID[6];
```

The node identifier is represented by six bytes corresponding to the ESP32's Wi-Fi MAC address.

The current generation function obtains the address using:

```cpp
WiFi.macAddress(packet.nodeID);
```

This requires the ESP32 Wi-Fi interface to be appropriately initialized and the necessary Wi-Fi declarations to be available through the project headers.

The same interface and MAC-address selection must be used consistently. The Wi-Fi station MAC and access-point MAC can differ, so the system should not alternate between them.

A MAC address provides a convenient identifier for the prototype, although it should not be treated as a cryptographically authenticated identity.

### 4.5 Sequence number

```cpp
uint16_t sequenceNumber;
```

The sequence number is a 16-bit unsigned integer that increases whenever `generateSequenceNumber()` is called.

It can support:

- Identifying the order of received beacons.
- Detecting possible missing beacon transmissions.
- Identifying repeated sequence values.
- Collecting data for packet delivery ratio (PDR) experiments.

The sequence number starts at zero internally. The first call increments it and returns `1`.

After reaching `65535`, unsigned 16-bit arithmetic wraps back to `0`.

Receivers must account for wraparound when comparing sequence numbers. A sequence number alone does not establish PDR accurately in every situation; the experiment must also define its observation window and how lost, delayed, duplicate, and out-of-order packets are treated.

### 4.6 Latitude and longitude

```cpp
int32_t latitude;
int32_t longitude;
```

Latitude and longitude are stored as signed 32-bit integers rather than floating-point values.

The encoding formula is:

\[
\text{Stored coordinate}
=
\operatorname{round}(\text{Coordinate in degrees}\times10^7)
\]

For example:

- Latitude: `6.9271000°` becomes `69271000`.
- Longitude: `79.8612000°` becomes `798612000`.

Negative values represent positions south of the Equator or west of the Prime Meridian, respectively.

During debugging, the values are converted back to decimal degrees by dividing by `10000000.0`.

This representation provides seven decimal places of coordinate resolution while keeping each coordinate field at four bytes.

### 4.7 Speed

```cpp
uint16_t speed;
```

Speed is stored in centimetres per second (cm/s).

The conversion from kilometres per hour is:

\[
\text{Speed}_{cm/s}
=
\frac{\text{Speed}_{km/h}\times100000}{3600}
\]

For example, a vehicle travelling at 45 km/h has a speed of:

\[
45\times\frac{100000}{3600}=1250\text{ cm/s}
\]

The maximum representable value of `uint16_t` is 65535 cm/s, or approximately 655.35 m/s. This range is sufficient for ordinary road-vehicle speeds.

The current `generateBeacon()` function initializes speed to zero. Actual speed data must be supplied before transmission.

### 4.8 Heading

```cpp
uint16_t heading;
```

The current header describes heading as a whole number of degrees in the range 0 to 359.

Under this convention:

- `0°` represents North.
- `90°` represents East.
- `180°` represents South.
- `270°` represents West.

The heading convention must match the mobile application's definition, which commonly uses degrees clockwise from North.

The current implementation does not preserve fractional-degree precision. If greater precision is required, the field can instead represent tenths of a degree, where `2755` means `275.5°`. This change would not alter the field's two-byte size, but it would require the header, conversion logic, and documentation to be updated together.

### 4.9 Timestamp

```cpp
uint32_t timestamp;
```

The timestamp is intended to represent Unix time in seconds, measured from 1 January 1970 UTC.

For a JavaScript-based mobile application, a Unix timestamp in seconds can be obtained with:

```typescript
const timestamp = Math.floor(Date.now() / 1000);
```

The application can send this value to the ESP32, which can then store it in the beacon packet.

The current implementation initializes the timestamp to zero. Actual time data must be supplied before transmission.

The timestamp represents the phone's system-clock time when the value is generated, not necessarily the exact time at which the GPS receiver measured the position. The application should define which event the timestamp represents.

Because the field is a 32-bit unsigned integer, its maximum value is `4294967295`. Unix time in seconds reaches this limit in February 2106. This is adequate for the present prototype.

---

## 5. Header File: `beacon.h`

### 5.1 Purpose

The header file defines the public interface and packet configuration for the Beacon Management subsystem.

It allows other source files, including `main.cpp`, to use beacon functions without needing to know how those functions are implemented.

### 5.2 Include guard

```cpp
#ifndef BEACON_H
#define BEACON_H

// Header declarations

#endif
```

The include guard prevents the header from being processed repeatedly within the same compilation unit.

Without it, repeated inclusion could lead to duplicate declaration problems.

### 5.3 Library headers

```cpp
#include "Library_Headers.h"
```

This includes the project's shared library headers. The shared header must provide the necessary declarations for the types and functions used by `beacon.h` and `beacon.cpp`.

Depending on the project's existing definitions, these include the fixed-width integer types, `size_t`, the Arduino `Serial` interface, the Wi-Fi interface, and the LoRa library.

The exact contents of `Library_Headers.h` should be checked to ensure that these dependencies are available.

### 5.4 Configuration constants

```cpp
constexpr uint8_t BEACON_PACKET_TYPE = 0x01;
constexpr uint8_t BEACON_MAC_SIZE = 6;
constexpr uint8_t BEACON_PACKET_SIZE = 25;
constexpr uint32_t BEACON_INTERVAL_MS = 5000;
```

These constants define the basic beacon configuration.

- `BEACON_PACKET_TYPE` identifies the message type.
- `BEACON_MAC_SIZE` defines the number of bytes in the node identifier.
- `BEACON_PACKET_SIZE` defines the serialized payload length.
- `BEACON_INTERVAL_MS` specifies a nominal beacon interval of 5000 milliseconds, equivalent to five seconds.

**Important:** Defining `BEACON_INTERVAL_MS` does not schedule transmissions by itself. The calling application must use this constant in a timer or scheduling mechanism.

The packet-size constant should also be checked against the field sizes whenever the packet format changes.

### 5.5 Structure definition

The `BeaconPacket` structure provides a convenient representation of the packet fields within the ESP32 program.

It is used by the generation, transmission, reception, and extraction functions.

The structure is not itself the wire format. The serialized byte array defines the actual payload sent over LoRa.

### 5.6 Public function declarations

The original header declares:

```cpp
uint16_t generateSequenceNumber();
bool transmitBeacon(BeaconPacket &packet);
bool receiveBeacon(BeaconPacket &packet);
```

These declarations expose the sequence-number, transmission, and reception functions to other source files.

The current `beacon.cpp` implementation also defines `generateBeacon()` and `extractBeaconInformation()`. Their declarations should therefore be added to the header:

```cpp
void generateBeacon(BeaconPacket &packet);
void extractBeaconInformation(const BeaconPacket &packet);
```

Without these declarations, other source files that include only `beacon.h` cannot safely use those functions through the intended public interface.

---

## 6. Source File: `beacon.cpp`

### 6.1 Purpose

The source file implements the actual Beacon Management operations.

Its functionality is organized into the following sections:

1. Sequence-number management.
2. Integer encoding and decoding.
3. Beacon serialization and deserialization.
4. Debug output.
5. Beacon generation.
6. Beacon transmission.
7. Beacon reception.
8. Beacon information extraction.

The implementation uses helper functions to keep the packet-processing logic separate from the LoRa communication operations.

### 6.2 Anonymous namespace

```cpp
namespace
{
    uint16_t sequenceNumber = 0;

    // Helper functions
}
```

The anonymous namespace limits the visibility of its contents to the current source file.

The sequence-number variable and helper functions are implementation details and do not need to be exposed to `main.cpp`.

This reduces the chance that other modules will depend directly on internal implementation details.

### 6.3 Integer encoding functions

The functions `writeUint16()` and `writeUint32()` convert integer values into bytes.

For example, the 16-bit value `0x1234` is encoded as:

```text
34 12
```

The 32-bit value `0x12345678` is encoded as:

```text
78 56 34 12
```

This is little-endian encoding, where the least-significant byte is transmitted first.

The encoding is performed explicitly, so the byte representation does not depend on the compiler's layout of the `BeaconPacket` structure.

### 6.4 Integer decoding functions

The functions `readUint16()` and `readUint32()` perform the reverse operation.

They read individual bytes from the payload buffer and combine them using bit shifts and bitwise OR operations.

For example, the bytes:

```text
34 12
```

are reconstructed into the 16-bit value:

```text
0x1234
```

The read functions use the same little-endian convention as the write functions.

Both operations maintain an `offset` variable to track the next byte to read or write.

The offset is passed by reference, allowing each helper function to advance the position for the calling function.

### 6.5 Serialization

```cpp
void serializeBeacon(
    const BeaconPacket &packet,
    uint8_t *buffer
);
```

Serialization converts the in-memory `BeaconPacket` structure into the byte array that will be transmitted.

The function writes fields in the following order:

1. Packet Type.
2. Node ID.
3. Sequence Number.
4. Latitude.
5. Longitude.
6. Speed.
7. Heading.
8. Timestamp.

The multi-byte integer fields are encoded in little-endian order.

The `const` reference ensures that the function can read the packet without modifying it. The buffer pointer identifies the destination memory in which the encoded bytes are stored.

The function is private to `beacon.cpp`, because it is placed inside the anonymous namespace.

### 6.6 Deserialization

```cpp
void deserializeBeacon(
    const uint8_t *buffer,
    BeaconPacket &packet
);
```

Deserialization reconstructs a `BeaconPacket` from a received byte array.

It reads the fields in precisely the same order used by `serializeBeacon()`.

The signed latitude and longitude fields are reconstructed from their 32-bit representations and stored in `int32_t` variables.

This process allows the receiver to use the decoded information as ordinary application data instead of manually manipulating bytes throughout the rest of the program.

The receiver must use the same packet layout and byte order as the transmitter.

### 6.7 Debug output

```cpp
void printBeacon(const BeaconPacket &packet);
```

The `printBeacon()` helper displays the packet fields in the Serial Monitor.

It prints:

- Node ID in hexadecimal.
- Sequence number.
- Latitude and longitude in decimal degrees.
- Speed in cm/s.
- Heading in degrees.
- Timestamp in Unix seconds.

For MAC-address formatting, it adds a leading zero to hexadecimal values smaller than `0x10`. For example, the byte `0x0A` is displayed as `0A`.

The helper is useful during development and hardware testing because it allows the developer to inspect the contents of an outgoing or decoded beacon.

It is an internal helper rather than part of the public interface.

### 6.8 Sequence-number generation

```cpp
uint16_t generateSequenceNumber()
{
    sequenceNumber++;
    return sequenceNumber;
}
```

This function increments the internal sequence-number counter and returns the updated value.

Because the counter is an unsigned 16-bit integer, it wraps around after `65535`.

The function is called by `transmitBeacon()` before serialization, ensuring that each transmission attempt receives a new sequence number.

The sequence number identifies the order of outgoing beacon attempts. It does not independently guarantee that a receiver has received the corresponding beacon.

### 6.9 Beacon generation

```cpp
void generateBeacon(BeaconPacket &packet)
{
    packet.packetType = BEACON_PACKET_TYPE;
    WiFi.macAddress(packet.nodeID);
    packet.latitude = 0;
    packet.longitude = 0;
    packet.speed = 0;
    packet.heading = 0;
    packet.timestamp = 0;
}
```

This function initializes the beacon structure.

Its responsibilities are:

1. Set the packet type to the beacon identifier.
2. Retrieve the ESP32 Wi-Fi MAC address and store it in the node identifier.
3. Initialize latitude and longitude.
4. Initialize speed and heading.
5. Initialize the timestamp.

The current location, speed, heading, and timestamp assignments are placeholders. They do not represent actual vehicle information.

Before integrating this function with the mobile application, the relevant fields must be populated using the GPS and time information received over Wi-Fi.

The function does not assign a sequence number. That is intentional in the current design because `transmitBeacon()` assigns the sequence number immediately before serializing the packet.

### 6.10 Beacon transmission

```cpp
bool transmitBeacon(BeaconPacket &packet);
```

This function handles the transmission of an outgoing beacon through the LoRa module.

Its processing steps are:

**Step 1: Set the packet type**

The function sets `packet.packetType` to `BEACON_PACKET_TYPE`.

**Step 2: Assign a sequence number**

The next sequence number is generated and stored in the packet.

**Step 3: Allocate a payload buffer**

```cpp
uint8_t payload[BEACON_PACKET_SIZE];
```

The buffer stores the serialized beacon bytes. Its size must match the agreed packet format.

**Step 4: Serialize the packet**

The `serializeBeacon()` function encodes the structure into the payload buffer.

**Step 5: Start the LoRa packet**

```cpp
LoRa.beginPacket();
```

The function checks the return value to determine whether the LoRa library successfully began preparing the outgoing packet.

**Step 6: Write the payload**

```cpp
const size_t bytesWritten =
    LoRa.write(payload, sizeof(payload));
```

This writes the serialized bytes into the outgoing LoRa packet.

**Step 7: Complete transmission**

```cpp
const int result = LoRa.endPacket();
```

With the standard Sandeep Mistry LoRa library in its default blocking mode, a return value of `1` indicates successful completion of the library's transmission operation.

**Step 8: Report the result**

The function checks the number of bytes written and the result returned by `LoRa.endPacket()`.

It returns `true` if the operation reports success and `false` if an error is detected.

A successful return does not confirm reception by another vehicle. It only indicates that the local transmission operation reported success.

### 6.11 Beacon reception

```cpp
bool receiveBeacon(BeaconPacket &packet);
```

This function checks for an incoming LoRa packet, validates its size, decodes its contents, and verifies its packet type.

**Step 1: Check for a packet**

```cpp
const int packetSize = LoRa.parsePacket();
```

The LoRa library returns zero when no packet is available. In that case, the function returns `false`.

**Step 2: Validate the packet size**

The received payload length is compared with `BEACON_PACKET_SIZE`.

Packets with an unexpected length are discarded and reported through the Serial Monitor.

This prevents a payload of an incompatible length from being decoded as a beacon.

**Step 3: Read the payload**

The function reads the expected number of bytes into a temporary buffer.

**Step 4: Deserialize the payload**

The `deserializeBeacon()` function reconstructs the `BeaconPacket` structure from the received bytes.

**Step 5: Validate the packet type**

The decoded packet type is compared with `BEACON_PACKET_TYPE`.

If the type does not match, the function rejects the packet.

**Step 6: Read radio reception metrics**

The function retrieves:

```cpp
LoRa.packetRssi();
LoRa.packetSnr();
```

RSSI represents the received signal strength, while SNR represents the signal-to-noise ratio.

These metrics can support subsequent link-quality assessment. They are not currently stored in the `BeaconPacket` structure because they are measurements associated with reception rather than fields transmitted by the sender.

**Step 7: Return the result**

The function returns `true` when the packet passes the implemented length and packet-type checks. Otherwise, it returns `false`.

The current implementation does not verify the plausibility of every decoded field, authenticate the sender, or reject duplicate sequence numbers. Those checks can be added if required by the system design.

### 6.12 Beacon information extraction

```cpp
void extractBeaconInformation(const BeaconPacket &packet)
{
    Serial.println("Received beacon:");
    printBeacon(packet);
}
```

This function displays the information already decoded into a `BeaconPacket` structure.

It calls `printBeacon()` to display the node identifier, sequence number, coordinates, speed, heading, and timestamp.

The function does not receive a LoRa packet or perform deserialization itself. It operates on an already populated structure.

In the current design, `receiveBeacon()` and `extractBeaconInformation()` are separate. The calling application should invoke the extraction function after successful reception.

---

## 7. Beacon Timing and Scheduling

The header defines:

```cpp
constexpr uint32_t BEACON_INTERVAL_MS = 5000;
```

This represents a nominal beacon interval of five seconds.

However, the supplied `beacon.cpp` does not contain a scheduler or timer that automatically transmits a beacon every five seconds.

The main application must manage the timing and call the beacon functions.

For example, the application-level logic could follow this sequence:

```cpp
// Conceptual example for main.cpp

BeaconPacket outgoingBeacon;

generateBeacon(outgoingBeacon);

// Populate the vehicle-data fields here.

transmitBeacon(outgoingBeacon);
```

The application would need to invoke this sequence according to its selected beacon interval. It should also update the vehicle data before each transmission.

For reception, the main application can repeatedly check for incoming packets:

```cpp
BeaconPacket incomingBeacon;

if (receiveBeacon(incomingBeacon))
{
    extractBeaconInformation(incomingBeacon);
}
```

The receiving function returns `false` when no packet is available, so the application must continue checking for incoming beacons.

The final scheduling design should avoid unnecessary blocking delays that could interfere with Wi-Fi data reception or other time-sensitive tasks.

---

## 8. Integration with the Mobile Application

The intended AutoLink RadioSense architecture uses Wi-Fi for transferring vehicle data from the mobile application to the ESP32 and LoRa for broadcasting beacons between vehicles.

The mobile application can provide:

- Latitude.
- Longitude.
- Speed.
- Heading.
- Timestamp.

The ESP32 obtains its node identifier from its Wi-Fi MAC address and maintains the sequence number locally.

The integration process should be:

1. The mobile application obtains the latest location and motion data.
2. It generates or supplies a Unix timestamp.
3. It sends the values to the ESP32 through the established Wi-Fi communication interface.
4. The ESP32 parses and validates the received values.
5. The values are converted into the packet's defined units.
6. The fields are populated in the `BeaconPacket` structure.
7. `transmitBeacon()` assigns the sequence number, serializes the structure, and transmits it through LoRa.

The current `generateBeacon()` implementation is only an initialization function. It must be extended or complemented by a data-population function to use real mobile application data.

The ESP32 should not transmit placeholder zero coordinates as though they were valid GPS measurements. The application should define how missing, invalid, or stale location data is handled.

---

## 9. Dependencies and Responsibilities

### 9.1 Hardware dependencies

The implementation assumes that the ESP32 and SX1278 LoRa module are connected and configured correctly.

The LoRa radio must be initialized before `transmitBeacon()` or `receiveBeacon()` is called.

The existing `LoRa_Manager` module is responsible for radio initialization and configuration. Beacon Management uses the initialized LoRa interface rather than duplicating radio initialization.

### 9.2 Software dependencies

The implementation relies on:

- Arduino framework support for `Serial`.
- ESP32 Wi-Fi support for retrieving the MAC address.
- The Sandeep Mistry LoRa library for packet transmission and reception.
- Standard integer types and `size_t`.
- The project-wide declarations provided by `Library_Headers.h`.

The build configuration must ensure that `beacon.cpp` is included in the selected PlatformIO environment and that the required library declarations are available.

### 9.3 Module responsibilities

| Responsibility | Assigned component |
|---|---|
| Initialize the SX1278 radio | `LoRa_Manager` |
| Define the beacon packet format | `beacon.h` |
| Initialize beacon fields | `generateBeacon()` |
| Generate sequence numbers | `generateSequenceNumber()` |
| Encode the packet | `serializeBeacon()` |
| Transmit the packet | `transmitBeacon()` |
| Receive the packet | `receiveBeacon()` |
| Decode the packet | `deserializeBeacon()` |
| Display decoded fields | `extractBeaconInformation()` and `printBeacon()` |
| Obtain and transfer mobile GPS data | Mobile application and ESP32 application-level integration |
| Maintain neighbour records | Neighbour Management subsystem |
| Evaluate links and select relays | Link Quality Assessment and Multihop Forwarding subsystems |

---

## 10. Current Implementation Limitations and Recommended Improvements

The implementation provides the core packet-processing and LoRa communication functions. The following details should be addressed before considering the subsystem fully integrated.

### 10.1 Placeholder vehicle data

The current generation function assigns zero to latitude, longitude, speed, heading, and timestamp.

**Required improvement:** Populate these fields using validated data received from the mobile application before transmission.

### 10.2 Missing public declarations

The current `beacon.h` declarations do not include `generateBeacon()` or `extractBeaconInformation()`, although both functions are implemented in `beacon.cpp`.

**Required improvement:** Add both declarations to `beacon.h`.

### 10.3 Incomplete-packet cleanup

If `receiveBeacon()` unexpectedly runs out of available bytes while reading a payload, it returns immediately without draining any remaining bytes.

**Recommended improvement:** Clear any remaining received bytes before returning from this error path, so the receiver is left in a clean state.

### 10.4 Packet field validation

The receiver validates packet size and type, but does not check whether decoded coordinates, speed, heading, or timestamp are plausible.

**Recommended improvement:** Add field validation where appropriate. For example, latitude should be within −90° to +90°, longitude within −180° to +180°, and heading should follow the selected range and units.

### 10.5 Sequence-number interpretation

The sequence number wraps around after 65535. A receiver must account for this when checking ordering or identifying missing beacons.

**Recommended improvement:** Define the sequence-comparison logic in the future Neighbour Management or measurement code, particularly if packet delivery ratio is evaluated.

### 10.6 Heading precision

The current implementation uses whole degrees.

**Recommended improvement:** Decide whether whole-degree precision is sufficient or whether heading should be represented in tenths of a degree. Keep the mobile application, packet specification, and receiver consistent.

### 10.7 Error handling

The current implementation reports transmission and reception errors through the Serial Monitor.

**Recommended improvement:** Retain useful diagnostic messages during hardware testing. If the subsystem is later used in a larger application, consider separating routine debug logging from operational error reporting.

### 10.8 Reception measurements

RSSI and SNR are printed during reception but are not returned to the caller or stored in a neighbour table.

**Recommended improvement:** Pass the reception metrics to the Link Quality Assessment or Neighbour Management subsystem when that integration is implemented.

### 10.9 Serialization consistency

The current implementation uses little-endian byte encoding.

**Required practice:** Document this choice and ensure every participating node uses the same field order, data types, scaling factors, and byte order.

The conversion of reconstructed unsigned coordinate bit patterns back to signed 32-bit integers is suitable for the intended ESP32 environment, which uses the expected two's-complement representation. For strict cross-platform portability, signed-value encoding and decoding can be implemented with explicitly defined bit-preserving conversions.

### 10.10 Reception and duplicate handling

A correctly sized packet with the beacon packet type is accepted even if it contains a repeated sequence number or implausible field values.

**Recommended improvement:** Add duplicate detection, freshness checks, and field validation in the relevant processing layer when required by the research design.

---

## 11. Testing and Verification

The subsystem should be tested incrementally using the existing PlatformIO test environments.

### 11.1 Packet encoding and decoding tests

Verify that a known `BeaconPacket` can be serialized and then deserialized without changing its field values.

Suggested test values include:

- Packet type: `0x01`.
- Node ID: a known six-byte test address.
- Sequence number: `1234`.
- Latitude: `69271000`.
- Longitude: `798612000`.
- Speed: `1250` cm/s.
- Heading: `90` degrees.
- Timestamp: a known nonzero Unix timestamp.

Compare the original and decoded values.

The coordinate values should return to approximately `6.9271000°` and `79.8612000°` when converted back to decimal degrees.

### 11.2 Sequence-number tests

Verify that:

1. The first generated number is `1`.
2. Subsequent calls increment the number.
3. The counter wraps from `65535` to `0`.
4. Each call to `transmitBeacon()` assigns a fresh sequence number.

### 11.3 Transmission tests

Using two ESP32 and SX1278 nodes, verify that:

1. The transmitter initializes the LoRa radio successfully.
2. The beacon payload has the expected length of 25 bytes.
3. The transmission function reports its result.
4. The receiver obtains the expected packet.
5. The decoded field values match the transmitted values.

### 11.4 Reception validation tests

Verify that the receiver rejects:

- Packets with an incorrect payload length.
- Packets with an unsupported packet type.

Also verify that no-packet conditions do not produce misleading reception messages.

### 11.5 Mobile application integration tests

After integrating Wi-Fi data transfer, verify that the transmitted beacon contains the actual mobile application's coordinates, speed, heading, and timestamp rather than placeholder zero values.

Check the unit conversions independently before testing the complete transmission path.

### 11.6 Radio metrics tests

Confirm that RSSI and SNR are reported for received packets. Record these values during controlled experiments to support subsequent link-quality analysis.

---

## 12. Summary

The Beacon Management subsystem provides the foundational communication mechanism for AutoLink RadioSense. It defines a structured beacon packet, encodes the fields into a fixed-length payload, transmits the payload through LoRa, receives incoming packets, and decodes valid beacons for application use.

The separation between the header and source file keeps the packet interface distinct from its implementation. The serialization and deserialization functions ensure that the transmitted payload follows a consistent byte-level format, while the transmission and reception functions handle communication through the LoRa library.

The current implementation establishes the basic mechanism but still requires integration with actual mobile application data, completion of the public header declarations, and appropriate handling of invalid or stale data.

Once these items are addressed and the subsystem is verified through hardware testing, its decoded beacon information can be passed to Neighbour Management and Link Quality Assessment, supporting the larger multi-hop routing and relay mechanism of AutoLink RadioSense.
