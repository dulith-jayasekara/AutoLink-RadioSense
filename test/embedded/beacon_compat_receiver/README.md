# Binary beacon compatibility receiver

Receive-only isolated ESP32 DOIT DevKit V1 / SX1278-Ra-02 diagnostic.
Uses repository LoRa library 0.8.0. No SCF buffer, hazard wrapping, ACKs,
transmission, or changes to existing projects. Local compilation is not hardware
verification. Hardware testing requires approval.

## Wire format

Exactly 25 bytes; offsets below are zero-based. No packed struct casts.

| Offset | Field | Encoding |
|---|---|---|
| 0 | packetType | uint8, must equal 0x01 |
| 1–6 | nodeID | six Wi-Fi MAC bytes, printed in wire order |
| 7–8 | sequenceNumber | uint16 little-endian |
| 9–12 | latitude | signed int32 little-endian, degrees x 10^7 |
| 13–16 | longitude | signed int32 little-endian, degrees x 10^7 |
| 17–18 | speed | uint16 little-endian, cm/s |
| 19–20 | heading | uint16 little-endian, degrees |
| 21–24 | timestamp | uint32 little-endian, Unix seconds |

Decoder requires latitude [-90,90], longitude [-180,180], heading [0,359],
and a nonzero unicast MAC (locally administered MACs are accepted). Heading 360
or sentinel headings are rejected under this profile; confirm any teammate
sentinel convention before interpreting a FAIL. Sequence 0 and rollover are valid.
Speed accepts the full uint16 range; no arbitrary vehicle-speed cutoff is used.
Timestamp 0 means unsynchronized and is accepted. Other timestamps are printed
without comparing Unix seconds to local millis(). No freshness or GPS-fix claim
is made. Coordinates (0,0) are numerically valid and get an informational notice.

## Radio compatibility and CRC gate

NSS=5, RST=14, DIO0=26, SCK=18, MISO=19, MOSI=23. Use the known working
3.3 V wiring, common ground, and 433 MHz antennas.
433 MHz, SF7, BW125 kHz, CR4/5, sync=0x12, preamble=8, explicit header.

This branch's `src/Core/LoRa_Manager.cpp` calls `LoRa.enableCrc()`.
The teammate's exact beacon initialization code has not yet been provided or
verified. **Before hardware testing, inspect the actual teammate transmitter's
initialization and confirm CRC and all radio settings.** This project provisionally
sets `BEACON_CRC_ENABLED=1` in its own platformio.ini. If the teammate explicitly
disables CRC, change this isolated flag to 0 and rebuild after confirming that
configuration. Do not treat this flag as proof that all received packets carry CRC:
in explicit-header LoRa, payload CRC presence is signaled by the transmitter.
The library does not expose CRC-failed packets through parsePacket(), so software
receive totals exclude those packets and cannot measure packet error rate.

## Build (no upload)

From `D:\AutoLink-RadioSense` in PowerShell:

```powershell
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/beacon_compat_receiver -e beacon_compat_receiver
```

## Upload and monitor (only after approval and CRC confirmation)

COM4 is an example; identify the receiver's actual port first.

```powershell
& $pio run -d test/embedded/beacon_compat_receiver -e beacon_compat_receiver -t upload --upload-port COM4
& $pio device monitor --port COM4 --baud 115200
```

Start the teammate's beacon transmitter. Expect a nominal 5000 ms interval;
actual scheduling, losses, and other transmitters can change observed intervals.
The diagnostic reports intervals between all received RF packets, not per-MAC
delivery statistics. No timing tolerance causes a decoding failure.

For screenshots, capture initialization PASS, raw HEX and exactly 25-byte length,
RSSI/SNR, decoded MAC/sequence/coordinates/speed/heading/timestamp, and
`PASS: EXACT 25-BYTE BEACON DECODED AND FIELDS VALIDATED`.
The RF reception PASS alone does not prove a valid beacon; decoding has its own
PASS/FAIL. Totals distinguish packets received, decoded, and rejected.
No packets means waiting, not a fabricated hardware PASS.

## Host decoder tests

The tests compile the same header used by firmware and require no board or radio.
They use literal golden bytes, endian/sign checks, short/long/null input,
wrong type, invalid MAC, coordinate boundaries, heading bounds, zero timestamp,
zero sequence, maximum speed, and signed-int minimum.

```powershell
$compiler = "C:\msys64\ucrt64\bin\g++.exe"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
$testExe = Join-Path $env:TEMP "beacon_decoder_test.exe"
& $compiler -std=c++11 -Wall -Wextra -pedantic -I test/embedded/beacon_compat_receiver/include test/embedded/beacon_compat_receiver/tests/decoder_test.cpp -o $testExe
if ($LASTEXITCODE -ne 0) { throw "Host compilation failed" }
& $testExe
if ($LASTEXITCODE -ne 0) { throw "Host tests failed" }
```

## Troubleshooting

- Initialization FAIL: check 3.3 V power, ground, SPI pins and module connections;
  reset after fixing wiring. Initialization failure does not claim RF reception.
- No packets: confirm teammate firmware is running and compare frequency, SF,
  bandwidth, coding rate, sync word, preamble, explicit-header mode and CRC code.
  Keep antennas attached and test at a modest separation.
- Wrong length/type: compare raw HEX with the table; text packets, SCF DATA, struct
  padding, and different serialization are incompatible. Do not relax the 25-byte check.
- Invalid coordinates/heading/MAC: check offsets, little-endian serialization,
  coordinate scaling, GPS validity and any sentinel conventions with the teammate.
- Timestamp 0 is accepted; it means clock synchronization is unavailable.
- Missing or variable intervals do not independently establish a decoder fault.
- Close another monitor before uploading; use the correct port and 115200 baud.
