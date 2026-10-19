# Receive-Only LoRa to SCF Buffer Diagnostic

This isolated ESP32 project receives real SX1278/Ra-02 packets and exercises the existing production `SCF_Buffer` and `SCF_DuplicateTracker` APIs. It does not modify production code and never transmits RF or sends ACKs.

## Important diagnostic distinction

The existing friend packet `Hello from AutoLink RadioSense!` is plain text and is not a proven SCF wire packet. Plain text is therefore wrapped in a diagnostic-only `scf::Message`:

- `sourceId = 0x54455354` (`TEST`)
- `sequence = local received RF packet number`, starting at 1
- `createdAt = millis()` at reception
- `expiresAt = createdAt + 300000 ms` (TTL: 300,000 ms / 5 minutes)
- `priority = Normal`
- `hopCount = 0`
- `hopLimit = 4`
- payload = the received bytes, only when length is at most 64 bytes

Repeated identical plain-text packets receive different sequence values and are treated as distinct diagnostic messages. They are not silently treated as duplicates. A duplicate status is possible only when the same `(sourceId, sequence)` identity is presented again, or when genuine SCF DATA frames repeat their encoded identity.

If a received payload starts with the SCF frame magic and is a valid DATA frame, the diagnostic attempts `SCF_Codec::decodeData()` and uses the encoded source, sequence, expiry, priority, hop information, and payload. It accepts only frames addressed to local node `1` or broadcast. ACK/NACK frames and malformed SCF-looking frames are reported and are not inserted into the buffer.

## Production APIs exercised

- `scf::Buffer::enqueue()` for admission and capacity/duplicate results.
- `scf::Buffer::find()` for stored identity and payload reporting.
- `scf::Buffer::count()` for retention status.
- `scf::Buffer::expire()` as the actual TTL maintenance path.
- `scf::DuplicateTracker::remember()` and `contains()` for identity history.
- `scf::isValidMessage()` for validation.
- `scf::decodeData()` for genuine SCF DATA frames.

The local Arduino LoRa library is compiled from the repository's existing `lib/LoRa/src` source. No `SCF_Radio_Adapter` is used for reception here because it owns a simulated-port abstraction; this diagnostic directly uses the existing LoRa receive API and production buffer/tracker APIs.

## Duplicate-history capacity policy

The production `DuplicateTracker` has capacity 16, no timestamp per identity, and no removal API. Production `Buffer::expire()` therefore does not remove duplicate-history entries. A full tracker is an explicit rejection, not an eviction.

This receive-only diagnostic adds a test-only retention policy: after the real production buffer reports count zero following TTL maintenance, it calls the production tracker's existing `clear()` API and logs that the diagnostic retention window ended. It never clears history while any buffered message remains. This permits a later packet after the diagnostic TTL window to be observed again, but a late duplicate of an expired packet could also be admitted as a new diagnostic event. That trade-off is intentionally limited to this test project and must not be treated as the final DTN duplicate policy.

## Build and upload

```powershell
Set-Location D:\AutoLink-RadioSense
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/lora_scf_buffer_test -e lora_scf_buffer_test
```

Upload only after reviewing the build:

```powershell
& $pio run -d test/embedded/lora_scf_buffer_test -e lora_scf_buffer_test -t upload --upload-port COM4
```

Monitor at 115200 baud:

```powershell
& $pio device monitor --port COM4 --baud 115200
```

This is receive-only single-node hardware testing. It does not prove multi-node SCF delivery, RF range, ACK exchange, custody transfer, or real sender identity interoperability. Buffer retention can be observed by stopping the sender and checking that the buffer count remains nonzero until the diagnostic TTL expires.
