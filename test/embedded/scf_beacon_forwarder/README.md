# Physical Beacon -> Store -> Forward diagnostic (Node B)

Node A keeps its existing 25-byte Beacon firmware. Node B uses this project's
`forwarder` firmware. Node C uses `../scf_forwarded_receiver` firmware.
No hardware result is claimed by compilation. Prior user-verified reception,
raw display, diagnostic wrapping, buffer lookup and tracker tests are preserved.

## Audit and preservation

The original platformio.ini and extra_sources.py were present; src/main.cpp was
zero bytes. Node C had only an empty src directory. The missing application logic
explains why these skeletons were not ready. How they became incomplete cannot
be established from the files alone.
All existing files were left untouched, including the empty main.cpp. Actual
Arduino setup/loop live in new src/forwarder.cpp. The existing script compiles
Message, Codec and Buffer but omits DuplicateTracker. A new compilation bridge
src/production_duplicate_tracker.cpp includes the unchanged production .cpp once;
it does not copy/reimplement the class or require changes to the existing script.
The new shared diagnostic header is also used by C. Both depend on the existing
beacon_compat_receiver/include/BeaconDecoder.h. Preserve that dependency when
later reviewing files for commit. No Scheduler, Reliability or RadioAdapter runs.

## Behavior and limits

Receive exactly 25 bytes, validate with existing Beacon decoder, preserve all bytes
in a production scf::Message payload, enqueue in scf::Buffer and verify lookup.
Beacon MAC/sequence are printed, but SCF identity is synthetic (TEST=0x54455354,
local monotonically increasing reception counter). Repeated identical raw Beacons
get distinct synthetic IDs: this is not proof of raw Beacon duplicate suppression.
Production DuplicateTracker checks/remembers those synthetic identities only.
Buffer capacity=8, history capacity=16. Full states reject; no eviction or automatic
history clearing is added. Expiry frees buffer space but not history. Reset B for
a new controlled run; identity history is not persistent across resets.

Type lowercase `f` to expire old messages, selectNext(now), copy the selected
stored bundle, increment outgoing hopCount from 0 to 1, encode genuine production
SCF DATA addressed to C=3, and transmit synchronously using local Arduino LoRa.
Encoded length is 28+25=53 bytes. Priority/expiry/identity ordering is the existing
buffer ordering. Repeated f can select the same bundle; no round-robin or automatic
forwarding is implied. Local TX completion retains the buffered original and is
not delivery confirmation. No ACK/NACK, automatic retry, encounter scheduler,
custody, persistence, or final team-protocol interoperability claim is added.

B stores creation/expiry in local millis with TTL=300000 ms (5 minutes).
Before sending, it copies remaining lifetime (expiry-now) into a diagnostic wire
duration profile: createdAt=0, expiresAt=remaining duration. Production codec layout
is unchanged. This preserves B storage age without assuming clocks agree. C checks
this explicit profile and never compares B uptime to C uptime. Time in transit is
unknown; this is not a general DTN end-to-end age solution or replay protection.
Deadline and sequence wrapping are refused; run short experiments on fresh resets,
not across the roughly 49.7-day millis rollover. Beacon Unix time is preserved;
zero remains accepted as unsynchronized. No Beacon is regenerated from decoded fields.

Radio: ESP32 DOIT DevKit V1 / SX1278-Ra-02, SCK18 MISO19 MOSI23 NSS5 RST14
DIO0=26; 433MHz SF7 BW125kHz CR4/5 sync0x12 preamble8 explicit header CRC on,
TX10dBm. CRC matches this branch's LoRa_Manager; actual Node A firmware remains
unverified. Confirm its settings, CRC and Beacon sentinel conventions before testing.
Use known working 3.3V/common-ground wiring and attached 433MHz antennas.

## Build only

From repository root in PowerShell (pio need not be on PATH):

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pio run -d test/embedded/scf_beacon_forwarder
& $pio run -d test/embedded/scf_forwarded_receiver
```

## Hardware procedure — requires explicit approval first

1. Confirm Node A radio/CRC code and identify the actual B and C serial ports.
2. Upload Node B's forwarder and Node C's forwarded_receiver only after approval.
   COM4 and COM5 below are examples; do not assume they match your devices.
3. Open separate monitors at 115200. Reset both. Start A and collect at least one
   valid Beacon at B. Capture B raw HEX, MAC/sequence, synthetic SCF ID, lookup PASS,
   TTL and nonzero buffer count. Avoid filling all eight slots unnecessarily.
4. Stop A, keep C listening, and wait (for example 10 seconds, less than TTL).
   Type f at B. This demonstrates later forwarding from stored bytes after source
   transmission stops. Observe remaining TTL below 300000 and retained buffer count.
5. Capture C's SCF decode and validation PASS, matching synthetic ID and all 25 HEX
   bytes from B, Beacon MAC/sequence, and C RSSI/SNR. C final PASS plus matching
   logs is reception evidence; B TX PASS alone is not.
6. For an actual carry/contact-disruption experiment, record B-C disconnection
   during A-B reception and restore B-C contact before f. Merely having three radios
   on a bench does not prove that A and C never had contact. Do not claim physical
   motion or routing performance without a documented controlled topology.

```powershell
& $pio run -d test/embedded/scf_beacon_forwarder -e forwarder -t upload --upload-port COM4
& $pio device monitor --port COM4 --baud 115200
```

Use another terminal for C commands in its README. Initial no-packet waiting is not
a FAIL. Invalid Beacon fields, wrong lengths and full buffers print FAIL. A visible
direct raw Beacon at C is ignored, never counted as forwarded DATA. If nothing is
received, check wiring, port, radio settings/CRC, antennas, sender state and distance.
If f reports no eligible message, check B admission logs and TTL; reset and repeat.
If C rejects DATA, compare source/destination/hops/duration and decoder field limits.
Raw payload comparison provides experimental evidence, not authentication.

Suggested screenshots: `SCF3_01_B_beacon_received_stored.png`,
`SCF3_02_A_stopped_B_retained.png`, `SCF3_03_B_manual_forward_53bytes.png`,
`SCF3_04_C_data_decode_payload_match.png`. Record the wait duration, device labels,
ports, topology and raw logs with each run. Hardware results remain pending.
