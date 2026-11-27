# Received Beacon Packet Validation

**Project:** AutoLink RadioSense\
**Status:** Proposed validation design; implementation and testing pending.

## 1. Objective

Validate received LoRa beacon packets before their information is used to create or update neighbour entries. Reuse existing GPS and Time Manager validation functions without modifying their implementations. Preserve the 25-byte serialized beacon format and retain useful link information when optional GPS measurements are unavailable.

## 2. Beacon packet structure

  ---------------------------------------------------------------------------------------------------------
  Field             Data type                         Size Representation
  ----------------- ---------------- --------------------- ------------------------------------------------
  Packet type       uint8_t                         1 byte Beacon identifier (0x01)

  Node ID           uint8_t\[6\]                   6 bytes Wi-Fi MAC address

  Sequence number   uint16_t                       2 bytes 0--65535

  Latitude          int32_t                        4 bytes Degrees × 10\^7

  Longitude         int32_t                        4 bytes Degrees × 10\^7

  Speed             uint16_t                       2 bytes Centimetres per second

  Heading           uint16_t                       2 bytes Whole degrees (0--359)

  Timestamp         uint32_t                       4 bytes Original GPS fix timestamp, Unix seconds

  **Total**                                   **25 bytes** Serialized payload, excluding compiler padding
  ---------------------------------------------------------------------------------------------------------

## 3. Packet-level validation

  -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  Check                   Validation logic                                                                                        Failure action
  ----------------------- ------------------------------------------------------------------------------------------------------- -----------------------------------------
  Packet size             Exactly 25 bytes                                                                                        Reject packet

  Completeness            All expected bytes must be available                                                                    Reject packet

  Packet type             Must equal BEACON_PACKET_TYPE                                                                           Reject packet

  Node ID                 Reject all-zero, broadcast and multicast MAC addresses; accept locally administered unicast addresses   Reject packet

  Own Node ID             Compare with local Wi-Fi MAC address                                                                    Reject packet

  Sequence number         Compare with previous accepted sequence for the same neighbour                                          Reject duplicate or out-of-order update
  -------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Size, completeness and type checks already exist in receiveBeacon(). Own-ID and sequence checks also exist in the supplied implementation. MAC format checks are proposed additions and require approval if the task is restricted to preserving existing behaviour.

### Sequence number validation

For an existing neighbour, calculate the unsigned 16-bit modular difference:

``` cpp
uint16_t difference = static_cast<uint16_t>(sequence - lastSequence);
```

-   Difference = 0: duplicate; reject update.
-   Difference = 1--32767: forward progression; accept.
-   Difference = 32768--65535: ambiguous or older sequence; reject update.
-   For a new neighbour, accept the initial sequence as the baseline.

This preserves rollover from 65535 to 0. For accepted forward progression, the estimated missing packet count is difference − 1. Existing PDR calculations remain unchanged. Remote node restart recovery is outside this design.

## 4. GPS field validation

Check reserved unavailable markers before converting fields or calling existing validation functions.

  -------------------------------------------------------------------------------------------------------------------------------------------
  Field                    Conversion / check              Existing function to reuse   Failure action
  ------------------------ ------------------------------- ---------------------------- -----------------------------------------------------
  Latitude and longitude   Divide both by 10000000.0       GPS_ValidatePosition()       Mark both coordinates unavailable

  Speed                    Divide by 100.0 to obtain m/s   GPS_ValidateSpeed()          Mark unavailable according to approved speed policy

  Heading                  Whole degrees; no scaling       GPS_ValidateHeading()        Mark heading unavailable

  Timestamp                Multiply seconds by 1000ULL     GPS_ValidateFixTimestamp()   Invalidate associated GPS data if invalid
  -------------------------------------------------------------------------------------------------------------------------------------------

### Position

GPS_ValidatePosition() calls GPS_ValidateLatitude() and GPS_ValidateLongitude(). Latitude must be within −90 to +90 degrees and longitude within −180 to +180 degrees. Both coordinates must be valid for a usable position. Use GPS_LATITUDE_UNAVAILABLE and GPS_LONGITUDE_UNAVAILABLE for unavailable coordinates.

### Speed

Zero is a valid stationary speed. GPS_SPEED_UNAVAILABLE is not a speed measurement. GPS_ValidateSpeed(double&, bool&) validates the converted speed and clamps excessive values to the existing configured maximum, reporting this through wasClamped.

The receiver policy for excessive speed (accept a clamped value or mark unavailable) must be explicitly finalized. Rejection of excessive remote speed is a proposed policy, not existing behaviour. Account for transmitter rounding to the nearest cm/s at the maximum threshold.

### Heading

GPS_ValidateHeading() accepts finite angles in \[0, 360). The encoded heading must therefore be 0--359 or GPS_HEADING_UNAVAILABLE. Zero represents North and must not be treated as unavailable.

## 5. GPS timestamp validation

The supplied generateBeacon() implementation transmits snapshot.fixTimestampMilliseconds / 1000ULL. Therefore the timestamp describes the GPS fix, not the beacon transmission time.

1.  Check GPS_TIMESTAMP_UNAVAILABLE and zero.
2.  Convert to milliseconds using static_cast`<uint64_t>`{=html}(timestamp) \* 1000ULL.
3.  Call GPS_ValidateFixTimestamp(), which internally uses Time_IsTimestampReasonable() and checks the reserved timestamp boundary.
4.  The existing supported Unix date range is 2020-01-01 through 2100-01-01 UTC.
5.  Check local clock availability using Time_IsSynchronized() and Time_GetCurrentTimestampMilliseconds().
6.  Compare remote fix time against local time. Avoid unsigned subtraction when the remote timestamp is ahead of the local clock.
7.  Calculate measurement age only when time comparison is meaningful.

A future timestamp tolerance and a policy for unsynchronized local clocks must be approved before implementing age-based acceptance. An unavailable local clock does not prove that the remote timestamp is invalid. Never synchronize the local clock using a received beacon.

## 6. GPS freshness and expiration

The existing adaptive position threshold functions can be reused with validated **remote** vehicle speed:

-   GPS_CalculatePositionFreshnessThresholdMs(speedMetersPerSecond)
-   GPS_CalculatePositionExpirationThresholdMs(speedMetersPerSecond)
-   GPS_DetermineFreshnessState(available, ageMs, freshThresholdMs, expireThresholdMs)

Measurement age = current local Unix time − remote GPS fix time (milliseconds).

For moving vehicles, the existing threshold functions calculate distance / speed × 1000 and bound the result using existing minimum and maximum thresholds. Stationary vehicles use the maximum threshold. Unavailable speed should be supplied as NAN to activate the existing fallback speed mechanism, not interpreted as zero or as the encoded sentinel.

  ------------------------------------------------------------------------------------------------------------------------------------
  Freshness state         Meaning                                                              Proposed handling
  ----------------------- -------------------------------------------------------------------- ---------------------------------------
  GPS_FRESH               Age below freshness threshold                                        Accept

  GPS_STALE               Age at or above freshness threshold but below expiration threshold   Retain with stale status

  GPS_EXPIRED             Age at or above expiration threshold                                 Mark affected measurement unavailable

  GPS_UNAVAILABLE         Measurement unavailable                                              Retain unavailable marker
  ------------------------------------------------------------------------------------------------------------------------------------

Speed and heading may reuse GPS_DetermineFreshnessState() with their existing field-specific thresholds, provided the transmitted fix timestamp represents those measurements. Confirm this against GPS_Manager.cpp and GPS_Manager.h before integration. Do not invent new thresholds.

Whole-second timestamp truncation can overestimate age by almost 1000 ms even with perfectly synchronized clocks. Clock differences add further uncertainty. The receiver age policy must account for both effects; no tolerance is finalized here.

## 7. GPS measurement ordering

Preserve the existing timestamp-history behaviour for each neighbour:

  -------------------------------------------------------------------------------------------------------------------------------
  Received GPS timestamp                   Action
  ---------------------------------------- --------------------------------------------------------------------------------------
  Newer than last accepted GPS timestamp   Update validated GPS measurements

  Equal to last accepted GPS timestamp     Do not refresh measurement age; allow unavailable fields to invalidate stored fields

  Older than last accepted GPS timestamp   Ignore GPS measurement update

  Unavailable or zero                      Invalidate associated GPS information
  -------------------------------------------------------------------------------------------------------------------------------

A new beacon sequence does not necessarily indicate a new GPS fix. Two distinct fixes within the same second may be indistinguishable with the current packet format.

## 8. Validation outcome and processing flow

1.  Receive LoRa packet and verify size and completeness.
2.  Deserialize the 25-byte payload.
3.  Validate packet type and node identity.
4.  Check sequence progression against neighbour history.
5.  Validate GPS field values and timestamp using existing functions.
6.  Evaluate GPS freshness where the local clock and measurement timestamp permit it, using approved age policies.
7.  Apply GPS timestamp ordering and unavailable markers.
8.  Supply accepted information for neighbour table updates, RSSI, SNR and PDR processing.

A structurally valid beacon with unavailable or expired GPS data may still provide useful neighbour presence and link quality information. Do not use invalid or freshness-unverified positions for geographic relay selection. Preserve the existing unavailable sentinel constants and avoid adding fields to the transmitted packet.

## 9. Limitations and scope

-   GPS accuracy is not transmitted and cannot be independently validated by the receiver.
-   A valid MAC address or plausible GPS reading does not establish authenticity.
-   GPS fix time is not beacon transmission time; beacon transit age cannot be determined independently.
-   Reception-time validation does not continuously expire stored neighbour GPS measurements between receptions. That is a separate future task.
-   Core GPS, Time and LoRa implementations remain unchanged.
-   Exact speed/heading thresholds, sentinel definitions and snapshot semantics require confirmation against the remaining headers and GPS Manager implementation.
-   New MAC format checks, remote age handling and speed acceptance policies must be distinguished from existing behaviour. No new thresholds or unrelated functionality are authorized by this document.

## 10. Verification scenarios

Test valid beacons, wrong packet size/type, incomplete payloads, invalid MAC addresses, self-ID, duplicate and older sequences, sequence rollover, coordinate boundaries, unavailable sentinels, stationary speed, excessive speed, heading 359/360, invalid timestamp ranges, unsynchronized local clock, future timestamps, repeated GPS fixes, and fresh/stale/expired measurement boundaries. Confirm that valid link information is retained when optional GPS data is invalid and that the serialized payload remains 25 bytes.
