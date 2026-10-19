# Component 4 Live ESP32 SCF Lifecycle Demo

This isolated PlatformIO project runs the real Component 4 SCF production sources on one ESP32. It simulates peer contact and ACK input through a local `scf::RadioPort` implementation. It never initializes LoRa, accesses SX1278 registers, transmits RF, or receives RF.

## Demonstrated lifecycle

- Creates and validates a hazard DATA message.
- Enqueues it through the real `RadioAdapter` and verifies buffer retention while disconnected.
- Simulates peer contact through `prepareEncounter()` and caller-driven `transmit()` using a no-RF fake port.
- Queues a real encoded ACK frame and processes it through `poll()`, `SCF_Codec`, `SCF_Scheduler`, and `SCF_Reliability`.
- Uses the real duplicate tracker through a second adapter instance receiving the same encoded DATA frame twice.
- Uses a separate expiring message and the real scheduler/buffer TTL path.

## Build and upload

```powershell
Set-Location D:\AutoLink-RadioSense
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/scf_lifecycle_demo -e scf_lifecycle_demo
```

Upload is intentionally not part of the automated workflow. After reviewing the generated files and successful build output:

```powershell
& $pio run -d test/embedded/scf_lifecycle_demo -e scf_lifecycle_demo -t upload --upload-port COM4
& $pio device monitor --port COM4 --baud 115200
```

This demo is single-node embedded execution with simulated peers and RF disabled. It does not verify multi-node LoRa delivery, RSSI/SNR, range, antenna behavior, or over-the-air ACK timing. The existing 32/32 diagnostic remains separate and unchanged.
