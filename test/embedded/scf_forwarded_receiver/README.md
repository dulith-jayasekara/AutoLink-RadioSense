# Node C: forwarded SCF DATA receiver

Use environment `forwarded_receiver` for the third ESP32. This is receive-only;
there is no SCF reforwarding, ACK or retry. Node A's direct 25-byte Beacons are
explicitly ignored as forwarding evidence. Valid production SCF DATA must target
node 3, carry synthetic TEST origin and nonzero sequence, hop 1/4, Normal priority,
25-byte payload and the diagnostic remaining-duration timestamp profile.
The existing Beacon decoder validates the embedded bytes. Final PASS requires
actual RF reception plus both decoding stages and field validation.

See [Node B README](../scf_beacon_forwarder/README.md) for architecture, unchanged
production sources, dependencies, radio pins/settings, CRC prerequisite, timestamp
limitations, controlled hardware steps, evidence and screenshot names. C does not
assume synchronized clocks or create an expiry deadline; it logs the received
remaining duration. Compare original HEX at B and C; no cryptographic verification
or physical isolation is claimed by a valid packet alone.

From repository root:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pio run -d test/embedded/scf_forwarded_receiver
```

Only after explicit hardware approval and confirmation of actual serial port:

```powershell
& $pio run -d test/embedded/scf_forwarded_receiver -e forwarded_receiver -t upload --upload-port COM5
& $pio device monitor --port COM5 --baud 115200
```

COM5 is an example. Close competing monitors before uploading. Confirm Node A CRC
and all radio settings before hardware testing. On silence, inspect B TX log and
radio configuration/wiring. A raw Beacon at C does not count; expect a 53-byte DATA
frame and `PASS: NODE C RECEIVED VALID FORWARDED SCF DATA AND ORIGINAL BEACON PAYLOAD`.
Capture the matching B synthetic identity, Beacon MAC/sequence and all 25 payload
bytes, plus C RSSI/SNR. Local builds do not verify delivery. No firmware has been
uploaded by preparation of this project.
