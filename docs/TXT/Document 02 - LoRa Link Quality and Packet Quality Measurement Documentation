## Document 02 - LoRa Link Quality and Packet Quality Measurement Documentation
# AutoLink RadioSense


## 1. Purpose

This document defines the measurements, parameters, and derived metrics that shall be collected during system-level testing of the LoRa communication link in the AutoLink RadioSense project.

The purpose of these measurements is to evaluate the quality, reliability, stability, propagation characteristics, and performance of a LoRa wireless link between two ESP32-based vehicle nodes.

The measurements obtained during these tests will later support the development and validation of the **Link Quality Assessment** subsystem and the **Multi-hop Forwarding / Relay Selection** mechanism.

The testing shall initially use raw LoRa packets without application-specific packet headers. As the project progresses, the same measurement framework shall be extended to the AutoLink RadioSense beacon packets.



# 2. Scope

The measurement framework covers:

1. Raw packet reception measurements
2. Radio signal measurements
3. Packet delivery measurements
4. Packet timing measurements
5. Link stability measurements
6. Packet integrity measurements
7. LoRa physical-layer configuration parameters
8. LoRa transmission characteristics
9. Throughput and goodput
10. Link availability
11. Distance and propagation-related measurements
12. Node mobility and relative movement
13. Statistical measurements derived from multiple packets

The measurements are divided into two categories:

### 2.1 Direct Measurements

These are measurements that can be obtained directly from the LoRa radio, packet reception process, or system hardware.

Examples:

* RSSI
* SNR
* Packet length
* Packet reception time
* CRC status
* LoRa configuration parameters

### 2.2 Derived Measurements

These are calculated from multiple packets or from a combination of measurements.

Examples:

* PDR
* Packet loss rate
* Average RSSI
* RSSI variation
* Average SNR
* SNR variation
* Inter-packet interval
* Packet latency
* Jitter
* Throughput
* Goodput
* Link availability
* Link stability



# 3. Test Architecture

The basic measurement system shall consist of at least two LoRa-enabled nodes.


+----------------------+              +----------------------+
|      Node A          |              |       Node B         |
|                      |              |                      |
| ESP32 + SX1278       |              | ESP32 + SX1278       |
|                      |              |                      |
| Transmitter          | -----------> | Receiver             |
|                      |    LoRa      |                      |
+----------------------+              +----------------------+
                                            |
                                            |
                                    Measurements
                                            |
                                            v
                                  Serial Monitor / Dataset


Node A shall transmit test packets containing sufficient information to identify each packet.

Node B shall receive the packets and record the available measurements.

For measurements such as PDR, packet loss, latency, and jitter, packet sequence numbers and/or timestamps shall be incorporated into the test packet when required.



# 4. Packet Identification

Each test packet should contain a unique sequence number when packet delivery performance is being evaluated.

Example:

```text
TEST_PACKET_001
TEST_PACKET_002
TEST_PACKET_003
TEST_PACKET_004
```

The sequence number allows the receiver to determine:

* Which packets were received
* Which packets were lost
* Whether packets were duplicated
* Whether packets arrived out of order

The sequence number is a testing mechanism and does not represent the final AutoLink RadioSense beacon packet format.



# 5. Radio Signal Quality Measurements

## 5.1 RSSI

**Received Signal Strength Indicator (RSSI)** represents the received signal strength at the receiver.

Example:

```text
RSSI = -87 dBm
```

RSSI shall be recorded for every successfully received packet whenever possible.

### Measurements

The system should record:

* Instantaneous RSSI
* Minimum RSSI
* Maximum RSSI
* Average RSSI
* RSSI variation
* RSSI standard deviation

### Purpose

RSSI can be used to evaluate:

* Received signal strength
* Signal attenuation
* Approximate propagation conditions
* Effects of distance
* Effects of obstacles
* Link degradation
* Link stability

RSSI should not be used as the sole indicator of link quality.



# 6. SNR

**Signal-to-Noise Ratio (SNR)** represents the relationship between the received LoRa signal and the noise level.

Example:

```text
SNR = 7.5 dB
```

SNR shall be recorded for each successfully received packet.

### Measurements

The system should calculate:

* Instantaneous SNR
* Minimum SNR
* Maximum SNR
* Average SNR
* SNR variation
* SNR standard deviation

### Purpose

SNR provides information about signal quality relative to noise and can help identify degradation caused by:

* Noise
* Interference
* Distance
* Obstructions
* Propagation conditions
* Mobility

RSSI and SNR should therefore be evaluated together.



# 7. Packet Length

Packet length represents the number of bytes contained in the received packet.

Example:

```text
Packet Length = 32 bytes
```

Packet length shall be recorded for every received packet.

### Purpose

Packet length is useful for:

* Verifying packet integrity
* Confirming that the expected payload was received
* Evaluating airtime
* Calculating throughput
* Calculating goodput
* Comparing different packet configurations

Packet length by itself is not a direct measurement of link quality.



# 8. Packet Reception Timestamp

The receiver shall record the local time at which each packet is successfully received.

Example:

```text
Packet: 001
Reception Time: 20:15:03.120
```

The timestamp provides the basis for calculating several timing-related metrics.



# 9. Inter-Packet Arrival Interval

The inter-packet interval represents the time between two consecutive successfully received packets.

```text
Inter-Packet Interval =
Reception Time(n) - Reception Time(n-1)
```

Example:

```text
Packet 001 → 20:15:03.120
Packet 002 → 20:15:04.135

Inter-Packet Interval = 1015 ms
```

This measurement can identify:

* Irregular packet arrival
* Missing packets
* Transmission timing variation
* Communication interruptions



# 10. Packet Delivery Ratio (PDR)

Packet Delivery Ratio is one of the most important link-quality metrics for the project.

```text
PDR =
Number of Successfully Received Packets
---------------------------------------- × 100
Number of Transmitted Packets
```

Example:

```text
Packets transmitted = 100
Packets received    = 96

PDR = 96%
```

PDR indicates how reliably a link delivers packets.

PDR shall be calculated over a defined measurement window.

Possible windows include:

* 10 packets
* 50 packets
* 100 packets
* 500 packets
* 1000 packets

The selected window should be documented for each experiment.



# 11. Packet Loss Rate

Packet Loss Rate represents the percentage of transmitted packets that were not successfully received.

```text
Packet Loss Rate =
Number of Lost Packets
---------------------- × 100
Number of Transmitted Packets
```

Alternatively:

```text
Packet Loss Rate = 100% - PDR
```

Example:

```text
PDR = 96%

Packet Loss Rate = 4%
```

PDR and Packet Loss Rate represent complementary measurements. Both do not need to be stored as independent raw measurements if one can be calculated from the other.



# 12. Missing Packet Detection

Packet sequence numbers shall be used to identify missing packets.

Example:

```text
Received:
001
002
003
005
006
008
```

The system shall identify:

```text
Missing:
004
007
```

Missing packet information is required for calculating packet loss and PDR.



# 13. Duplicate Packet Detection

The receiver shall optionally detect duplicate packets.

Example:

```text
001
002
003
003
004
```

Packet `003` has been received twice.

Duplicate packets may indicate:

* Retransmission behavior
* Application-level duplication
* Test-system errors
* Protocol behavior

Duplicate detection is primarily a communication/system-level measurement rather than a LoRa physical-layer measurement.



# 14. Out-of-Order Packet Detection

The sequence number can also be used to determine whether packets arrive in the expected order.

Example:

```text
001
002
004
003
005
```

Packet `004` arrived before packet `003`.

Out-of-order detection shall be recorded where the communication architecture permits such behavior.



# 15. Packet Integrity and CRC Status

LoRa CRC shall be enabled where appropriate.

The test system should distinguish between successfully decoded packets and packets affected by CRC errors whenever the SX1278 and software interface provide access to the relevant status information.

A CRC failure indicates that the received data did not pass packet integrity verification.

CRC information can therefore provide an additional indication of communication quality.

### Important consideration

The application normally receives successfully decoded packets. Packets failing CRC may not be delivered to the application in the same way as valid packets.

Therefore, detailed CRC-error statistics may require lower-level access to the SX1278 status/interrupt registers rather than relying only on the high-level packet reception function.



# 16. Packet Latency

Packet latency represents the time required for a packet to travel from transmitter to receiver.

```text
Latency =
Reception Timestamp - Transmission Timestamp
```

Example:

```text
Transmission Time = 20:15:03.100
Reception Time    = 20:15:03.142

Latency = 42 ms
```

Latency measurements require timestamps that are sufficiently synchronized between the transmitter and receiver.

For early experiments, controlled timing and relative measurements may be preferred over precise one-way latency measurement.

Latency is useful for evaluating:

* Communication responsiveness
* Application performance
* Multi-hop forwarding delay
* Suitability for time-sensitive messages



# 17. Packet Jitter

Jitter represents the variation in packet delay or packet arrival timing.

For a sequence of packets, the system shall calculate the variation between successive packet intervals or packet delays.

Example:

```text
Expected interval = 1000 ms

Observed:
1010 ms
985 ms
1032 ms
998 ms
```

Large variations indicate greater timing instability.

Jitter can be useful when evaluating:

* Beacon delivery consistency
* Communication stability
* Multi-hop delay variation



# 18. RSSI Variation

RSSI shall not only be considered as an individual value.

A measurement window should also calculate RSSI variation.

Example:

```text
Average RSSI = -88.3 dBm
Minimum RSSI = -96 dBm
Maximum RSSI = -81 dBm
```

RSSI variation can be calculated using statistical methods such as:

* Range
* Variance
* Standard deviation

High RSSI variation may indicate:

* Fading
* Mobility
* Obstructions
* Changing propagation conditions
* Unstable link conditions



# 19. SNR Variation

The same approach shall be applied to SNR.

Example:

```text
Average SNR = 7.8 dB
Minimum SNR = 3.2 dB
Maximum SNR = 10.2 dB
```

SNR variation can be used as an indicator of changing channel conditions.



# 20. RSSI and SNR Trends

The system should optionally analyze the trend of RSSI and SNR over time.

Example:

```text
Packet     RSSI       SNR
-----------------------------
001        -78 dBm    10.2 dB
002        -80 dBm     9.8 dB
003        -82 dBm     9.1 dB
004        -87 dBm     7.4 dB
005        -91 dBm     5.8 dB
006        -95 dBm     3.2 dB
```

A consistent decrease in RSSI and SNR can indicate that the nodes are moving away from one another or that propagation conditions are degrading.

The system may calculate:

* RSSI trend
* SNR trend
* Rate of RSSI change
* Rate of SNR change

These values may later support predictive link-quality assessment.



# 21. Link Stability

Link stability represents how consistently a communication link remains usable over a measurement period.

A stable link may demonstrate:

* High PDR
* Low packet loss
* Relatively stable RSSI
* Relatively stable SNR
* Low timing variation
* Few communication interruptions

An unstable link may demonstrate:

* Rapid RSSI changes
* Rapid SNR changes
* Increasing packet loss
* Low PDR
* Large timing variations
* Repeated communication interruptions

Link stability shall be evaluated over a defined observation window rather than from a single packet.



# 22. Link Availability

Link availability represents the percentage of a measurement period during which communication with the neighboring node remains usable.

Example:

```text
Test duration       = 10 minutes
Link available      = 8 minutes 42 seconds
Link unavailable    = 1 minute 18 seconds
```

Therefore:

```text
Link Availability = 87%
```

The exact definition of "available" shall be established by the test implementation. A possible criterion is that the link is considered available when packets are received successfully within a predefined timeout period.

Link availability is particularly relevant to mobile multi-hop communication because a relay may become unavailable as vehicles move.



# 23. Communication Interruption Duration

When the receiver stops receiving packets for longer than the predefined reception timeout, a communication interruption can be recorded.

The system may record:

```text
Interruption Start
Interruption End
Interruption Duration
```

Example:

```text
Link interruption:
Start    : 20:20:10
End      : 20:20:13

Duration : 3 seconds
```

This provides additional information about link stability.



# 24. LoRa Physical-Layer Configuration

The following parameters shall be recorded for every controlled experiment:

### Frequency

Example:

```text
433 MHz
```

### Spreading Factor

Example:

```text
SF7
```

Possible values include:

```text
SF6 – SF12
```

### Bandwidth

Example:

```text
125 kHz
```

### Coding Rate

Example:

```text
4/5
```

### Preamble Length

The configured LoRa preamble length shall be recorded.

### Header Mode

The experiment shall record whether explicit or implicit header mode is being used.

### CRC Configuration

The experiment shall record whether LoRa CRC is enabled.

These parameters are not packet-quality measurements themselves, but they are essential experimental configuration parameters because they directly affect LoRa propagation, data rate, sensitivity, airtime, and reliability.



# 25. Time-on-Air

Time-on-Air represents the amount of time required to transmit a LoRa packet.

It depends on parameters including:

* Payload length
* Spreading Factor
* Bandwidth
* Coding Rate
* Preamble length
* Header configuration

For example, the same payload can have significantly different airtimes under different spreading factors.

Time-on-Air is useful for evaluating:

* Channel occupancy
* Collision probability
* Transmission efficiency
* Energy consumption
* Beacon transmission frequency
* Network scalability

Time-on-Air may be calculated from the LoRa configuration and packet parameters.



# 26. Effective Data Throughput

Effective throughput represents the amount of successfully received payload data per unit of time.

```text
Throughput =
Successfully Received Payload Bits
-----------------------------------
Measurement Duration
```

Example:

```text
Successfully received data = 80,000 bits
Measurement duration        = 20 seconds

Throughput = 4 kbps
```

This represents actual received application throughput rather than only the theoretical LoRa data rate.



# 27. Goodput

Goodput represents the rate of successfully delivered useful application data.

Goodput differs from theoretical data rate because it can be reduced by:

* Packet loss
* Retransmissions
* Headers
* Protocol overhead
* Communication interruptions

The system shall calculate goodput where the test configuration allows useful payload data to be clearly separated from protocol overhead.



# 28. Distance Between Nodes

Distance between transmitter and receiver shall be recorded during propagation experiments.

Distance can be determined using:

* GPS coordinates
* Known physical test distances
* Other positioning systems

Example:

```text
Node A: 6.9271, 79.8612
Node B: 6.9280, 79.8620

Distance ≈ calculated geographic separation
```

Distance is particularly important when evaluating:

```text
Distance
      ↓
RSSI / SNR
      ↓
PDR
      ↓
Link Quality
```

Distance is not itself a radio link-quality measurement, but it is an essential propagation-related experimental parameter.



# 29. Node Position

For mobile V2V testing, the position of each node should optionally be recorded.

Possible fields include:

```text
Node ID
Latitude
Longitude
Altitude (optional)
Timestamp
```

This enables link-quality measurements to be associated with geographic locations.



# 30. Relative Movement

Because AutoLink RadioSense is intended for vehicular communication, node movement shall be considered.

The test system may record:

* Node speed
* Node heading
* Relative movement
* Distance change over time
* Relative direction

Example:

```text
Node A speed = 40 km/h
Node B speed = 35 km/h

Distance at t1 = 500 m
Distance at t2 = 450 m
Distance at t3 = 390 m
```

This indicates that the nodes are approaching each other.

Conversely:

```text
500 m → 600 m → 720 m
```

indicates that they are moving apart.

This information is important because link quality in a V2V network is dynamic.



# 31. Relative Distance Change

The rate at which the distance between nodes changes may be calculated.

```text
Distance Change =
Distance(t2) - Distance(t1)
```

A positive value may indicate increasing separation, while a negative value may indicate decreasing separation.

The rate of change can be calculated as:

```text
Distance Change Rate =
Distance Change / Time Interval
```

This may later be used as an additional indicator of expected link stability.



# 32. Link Quality Dataset

The system test framework should ultimately be capable of generating records similar to:

```text
Timestamp
Node ID
Packet ID
Packet Length
RSSI
SNR
Reception Status
CRC Status
Transmission Timestamp
Reception Timestamp
Packet Latency
Inter-Packet Interval
PDR
Packet Loss Rate
Jitter
Average RSSI
RSSI Variation
Average SNR
SNR Variation
Link Availability
Distance
Latitude
Longitude
Speed
Heading
Relative Movement
LoRa Frequency
Spreading Factor
Bandwidth
Coding Rate
Preamble Length
Time-on-Air
Throughput
Goodput
```

Not every field must be included in every individual test. Each system test shall collect the measurements relevant to its purpose.



# 33. Measurement Priority

The parameters shall be prioritized according to their relevance to the AutoLink RadioSense project.

## High Priority

These parameters are essential for the initial Link Quality Assessment experiments:

1. RSSI
2. SNR
3. Packet Length
4. Packet Sequence Number
5. Packet Reception Status
6. PDR
7. Packet Loss Rate
8. RSSI Average
9. SNR Average
10. RSSI Variation
11. SNR Variation
12. Link Stability
13. Link Availability

## Medium Priority

These measurements should be added as the testing framework matures:

14. Packet Reception Timestamp
15. Inter-Packet Interval
16. Packet Latency
17. Jitter
18. Duplicate Packet Detection
19. Out-of-Order Packet Detection
20. CRC Status
21. Communication Interruption Duration
22. Distance Between Nodes
23. Node Position
24. Node Speed
25. Node Heading
26. Relative Movement
27. Relative Distance Change

## Experimental / Performance Measurements

These should be included when evaluating overall LoRa communication performance:

28. Time-on-Air
29. Effective Throughput
30. Goodput

## Configuration Parameters

The following shall always be recorded as experimental configuration:

31. LoRa Frequency
32. Spreading Factor
33. Bandwidth
34. Coding Rate
35. Preamble Length
36. Header Mode
37. CRC Configuration



# 34. Relationship Between Measurements

The measurements should not be treated as isolated values.

The overall relationship can be represented as:

```text
                    Physical Environment
                           │
             ┌─────────────┼─────────────┐
             │             │             │
          Distance      Obstacles      Mobility
             │             │             │
             └─────────────┼─────────────┘
                           ↓
                    Radio Propagation
                           │
                  ┌────────┴────────┐
                  │                 │
                 RSSI              SNR
                  │                 │
                  └────────┬────────┘
                           ↓
                    Packet Reception
                           │
             ┌─────────────┼─────────────┐
             │             │             │
            PDR        Packet Loss     CRC
             │             │             │
             └─────────────┼─────────────┘
                           ↓
                    Link Stability
                           │
                           ↓
                  Link Quality Assessment
                           │
                           ↓
                   Relay Selection
```

This relationship is important because **no single measurement should be considered a complete representation of link quality**.



# 35. Link Quality Assessment Concept

The collected measurements will eventually support a composite Link Quality Assessment mechanism.

Conceptually:

```text
RSSI
SNR
PDR
Packet Loss
RSSI Stability
SNR Stability
Link Availability
        │
        ↓
Link Quality Assessment
        │
        ↓
Link Quality Score / Classification
        │
        ↓
Routing / Relay Selection
```

The exact weighting, thresholds, normalization methods, and scoring mechanism shall be determined experimentally and should not be hard-coded before sufficient test data has been collected.



# 36. Experimental Data Collection

System tests should produce structured output that can later be analyzed.

A CSV-compatible format is recommended.

Example:

```text
timestamp,packet_id,length,rssi,snr,pdr,latency,distance
20:15:03.120,001,32,-78,10.2,100,42,100
20:15:04.135,002,32,-80,9.8,100,45,100
20:15:05.149,003,32,-82,9.1,100,43,100
```

Serial Monitor output may be used during development, while structured logging should eventually be introduced for large-scale experiments.



# 37. Measurement Windows

Many link-quality metrics should be calculated over a defined number of packets rather than from a single packet.

Example:

```text
Measurement Window = 100 packets
```

Within the window:

```text
Average RSSI
Average SNR
RSSI variation
SNR variation
PDR
Packet loss rate
Average inter-packet interval
Jitter
```

At the end of each window, the system may produce a summary.

Example:

```text
========================================
LINK QUALITY WINDOW
========================================

Packets transmitted : 100
Packets received    : 96
PDR                 : 96%
Packet loss         : 4%

Average RSSI        : -88.3 dBm
Minimum RSSI        : -96 dBm
Maximum RSSI        : -81 dBm

Average SNR         : 7.8 dB
Minimum SNR         : 3.2 dB
Maximum SNR         : 10.2 dB

Average interval    : 1004 ms
Jitter              : 12 ms

Link availability   : 98%
========================================
```



# 38. System Test Progression

The measurements shall be introduced progressively through the system test programs.

## Test 01 – Raw Transmission

Verify:

* LoRa initialization
* Raw packet creation
* Raw packet transmission
* Transmission success/failure

## Test 02 – Raw Reception

Verify:

* Packet detection
* Payload reception
* Packet length
* RSSI
* SNR

## Test 03 – TX/RX Communication

Verify:

* Two-node communication
* Packet sequence
* Successful packet reception
* Basic RSSI/SNR collection

## Test 04 – Packet Loss / Delivery

Verify:

* Packet sequence numbers
* Missing packets
* PDR
* Packet loss rate

## Test 05 – Range

Evaluate:

* Distance
* RSSI
* SNR
* PDR
* Packet loss
* Communication range

## Test 06 – Repeated Transmission

Evaluate:

* Long-term packet transmission
* Packet reception consistency
* Radio stability

## Test 07 – Bidirectional Communication

Evaluate:

* Two-way communication
* Reception reliability in both directions

## Test 08 – RSSI/SNR Characterization

Evaluate:

* Average RSSI
* Average SNR
* RSSI variation
* SNR variation
* Trends

## Test 09 – Packet Delivery Performance

Evaluate:

* PDR
* Packet loss
* Duplicate packets
* Out-of-order packets

## Test 10 – Communication Stability

Evaluate:

* Link stability
* Link availability
* Communication interruptions
* RSSI/SNR trends
* Timing variation

## Test 11 – Beacon Transmission

Introduce:

* Node ID
* GPS
* Heading
* Speed
* Timestamp

## Test 12 – Beacon Reception

Measure:

* Beacon reception
* RSSI
* SNR
* Packet length
* Reception timestamp

## Test 13 – Beacon Information Extraction

Verify:

* Beacon field extraction
* Packet parsing
* Data validity

## Test 14 – Beacon Exchange

Evaluate:

* Vehicle-to-vehicle beacon exchange
* Link quality measurements
* Neighbor information



# 39. Important Distinction Between Packet Quality and Link Quality

The system shall distinguish between **packet-level measurements** and **link-level measurements**.

### Packet-Level

These describe an individual packet:

```text
Packet Length
RSSI
SNR
Reception Timestamp
CRC Status
Sequence Number
Latency
```

### Link-Level

These describe the communication relationship over a period:

```text
PDR
Packet Loss Rate
Average RSSI
Average SNR
RSSI Variation
SNR Variation
Jitter
Link Stability
Link Availability
Throughput
Goodput
```

This distinction is important because a single packet cannot reliably describe the overall quality of a dynamic V2V link.



# 40. Final Objective

The ultimate objective of this measurement framework is to provide experimentally validated information that can be used to determine whether a neighboring vehicle represents a suitable communication link and, subsequently, whether it is an appropriate relay candidate.

The system should therefore progress from:

```text
Raw LoRa Communication
        ↓
Packet Measurements
        ↓
Link Measurements
        ↓
Link Quality Assessment
        ↓
Geographic + Link-Based Evaluation
        ↓
Relay Selection
```

The collected experimental data shall be used to determine appropriate thresholds and decision criteria rather than selecting arbitrary values without experimental justification.



# 41. Summary of Parameters

The complete parameter set defined in this document is:

### Packet / Reception Parameters

* Packet payload
* Packet length
* Packet sequence number
* Packet reception status
* Packet reception timestamp
* CRC status
* Duplicate status
* Packet ordering

### Signal Parameters

* RSSI
* Minimum RSSI
* Maximum RSSI
* Average RSSI
* RSSI variation
* RSSI variance / standard deviation
* RSSI trend
* SNR
* Minimum SNR
* Maximum SNR
* Average SNR
* SNR variation
* SNR variance / standard deviation
* SNR trend

### Delivery Parameters

* Packets transmitted
* Packets received
* Packets lost
* PDR
* Packet loss rate
* Link availability
* Communication interruption duration

### Timing Parameters

* Transmission timestamp
* Reception timestamp
* Packet latency
* Inter-packet interval
* Jitter

### Propagation / Mobility Parameters

* Node position
* Distance between nodes
* Node speed
* Node heading
* Relative movement
* Relative distance change
* Rate of distance change

### Performance Parameters

* Time-on-Air
* Effective throughput
* Goodput

### LoRa Configuration Parameters

* Frequency
* Spreading Factor
* Bandwidth
* Coding Rate
* Preamble length
* Header mode
* CRC configuration

These parameters together form the measurement foundation for the AutoLink RadioSense LoRa link-quality experiments and subsequent Link Quality Assessment research.
