# M-12: Native SCF DATA over real LoRa

Isolated two-node diagnostic for ESP32 DOIT DevKit V1 + SX1278/Ra-02.
Uses unchanged production SCF_Message, SCF_Codec, and SCF_Buffer sources.
This project uses the real local Arduino LoRa library directly, following the
existing isolated diagnostics. SCF_LoRa_Port was inspected but is not used:
it assumes initialization by the shared LoRa manager, and this test needs
direct RSSI/SNR access and synchronous local transmission completion.
No shared application, production SCF code, or older diagnostic is modified.

## Hardware and settings

Both nodes: SCK=18, MISO=19, MOSI=23, NSS=5, RST=14, DIO0=26.
Use the known working 3.3 V supply, common ground, and 433 MHz antennas.
433000000 Hz, SF7, BW125000 Hz, CR4/5, sync word 0x12, preamble 8,
explicit header (library default), CRC enabled, TX power 10 dBm.
Sender source ID=1; receiver destination ID=2; sequence=1; High priority;
hopCount=0; hopLimit=4. Payload: `M12 native SCF hazard: road obstacle`.
Do not use the old text transmitter for this test.

## Independent clocks and TTL

This is an explicitly agreed diagnostic profile, not a new production protocol.
On wire, createdAt=0 and expiresAt=300000 express a five-minute duration.
The receiver validates these exact values with the production codec and checks
every expected field and payload before admission. It copies the decoded message
and changes only its local timestamps to reception millis() and reception+300000.
Identity, payload, priority, and hop fields are preserved. Both wire and local
timestamps are logged. The receiver never compares remote uptime to local uptime.
Duplicate admission uses production Buffer::enqueue(), which rejects the copy
without replacing the stored message or refreshing its deadline.

This approach does not account for sender storage time, RF transit time, or stale
replays; it is not end-to-end TTL preservation. It must not be used to claim a
general DTN age solution. A production solution needs an agreed age/remaining-life
scheme or synchronized time. Buffer expiry uses ordinary uint32 comparisons;
this diagnostic refuses admission if adding the TTL would wrap. Run short tests
on freshly reset devices, not across the approximately 49.7-day millis rollover.
There is no persistent duplicate history: after expiry or reset, the identity can
be admitted again. Reset both nodes between experiments; sequence 1 is deliberately
fixed for a single controlled run. There is no security/authentication guarantee.

## Build both roles (no upload)

From repository root in PowerShell:

```powershell
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/scf_native_data -e sender
& $pio run -d test/embedded/scf_native_data -e receiver
```

The default role is receiver. Each role has a separate build directory. Only
Message, Codec, and Buffer production modules are compiled alongside the local
LoRa library; no RadioAdapter, Scheduler, or Reliability behavior is enabled.

## Upload and monitor sender (only after hardware-test approval)

COM4 below is an example; identify the actual sender port first.

```powershell
& $pio run -d test/embedded/scf_native_data -e sender -t upload --upload-port COM4
& $pio device monitor --port COM4 --baud 115200
```

## Upload and monitor receiver (only after hardware-test approval)

COM5 below is an example; identify the actual receiver port first.

```powershell
& $pio run -d test/embedded/scf_native_data -e receiver -t upload --upload-port COM5
& $pio device monitor --port COM5 --baud 115200
```

## Controlled hardware procedure and screenshot evidence

1. After approval, upload each role to its corresponding board. Open two serial
   monitors. Reset both boards and wait for receiver initialization PASS and Ready.
2. In the sender monitor, type lowercase `s`. The sender encodes one valid message
   with the production codec, sends it, and sends the exact same byte array once
   more after three seconds. It then stops. This second send is a deliberate
   duplicate stimulus, not an ACK-driven retry or reliability implementation.
3. Capture sender identity, payload, TTL, codec PASS, and local transmit completion.
   Sender RSSI/SNR is N/A; those metrics belong to the receiver.
4. Capture receiver RSSI/SNR, field validation PASS, admission=Enqueued/count=1,
   then admission=Duplicate/count=1 and PASS for no TTL refresh. The summary is
   `PASS: M12 RF DATA ADMISSION AND DUPLICATE REJECTION`.
5. Optionally leave the receiver running for five minutes after first reception;
   capture the production buffer TTL expiry PASS. Reset both nodes for another run.

A local sender completion is not proof of delivery. If either packet is lost,
the receiver cannot complete the duplicate evidence; it waits without inventing
a PASS or claiming a timeout failure. Reset and repeat manually. Unrelated or
malformed RF frames produce FAIL logs and suppress the run's overall PASS until
reset. Logs show exact per-check evidence, not a statistical delivery result.
The receiver never sends RF, ACKs, or NACKs. This test does not establish retries,
multi-hop, custody transfer, range, delivery ratio, or research performance.
Hardware verification remains pending; successful local builds only prove compilation.
