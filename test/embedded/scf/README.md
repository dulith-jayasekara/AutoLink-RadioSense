# Component 4 SCF Embedded Diagnostic

This is a version-controlled ESP32 embedded diagnostic for Component 4. It links the real SCF production sources from `src/Store Carry Forward/` through `extra_sources.py`; the only new firmware source is the Serial test harness under `src/`.

The harness is single-node, uses no LoRa initialization, does not access radio registers, and does not transmit RF. It runs deterministic checks for message validation, payload boundaries, DATA codec round-trip and byte order, malformed DATA rejection, bounded buffer behavior, TTL expiry, duplicate detection, reliability retry and ACK matching, scheduler expiry, and hardware-neutral adapter polling.

The observed hardware result was **32 passed, 0 failed** using the external diagnostic project. That result is embedded-test evidence only. It is not over-the-air LoRa validation and does not establish multi-node delivery, RF range, packet delivery ratio, or research performance.

## Build

From PowerShell:

```powershell
Set-Location D:\AutoLink-RadioSense
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/scf -e scf_embedded_diag
```

## Upload after approval

```powershell
& $pio run -d test/embedded/scf -e scf_embedded_diag -t upload --upload-port COM4
```

## Monitor after upload

```powershell
& $pio device monitor --port COM4 --baud 115200
```

Expected output includes individual `PASS:` lines followed by:

```text
TOTAL PASSED: 32
TOTAL FAILED: 0
SCF EMBEDDED DIAGNOSTIC PASS
```

The diagnostic does not replace the separate SX1278 register-version diagnostic and does not modify production `main.cpp`, `platformio.ini`, `LoRa_Manager`, or other shared files.
