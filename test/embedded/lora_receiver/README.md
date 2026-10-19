# SX1278 Ra-02 Receive-Only Diagnostic

This isolated PlatformIO project listens for real LoRa packets using one ESP32 DOIT DevKit V1 and an SX1278/Ra-02 module. It reuses the repository's local Arduino LoRa library (`lib/LoRa`, version 0.8.0).

## Configuration

- Board: `esp32doit-devkit-v1`
- Serial/upload port: `COM4`
- Serial speed: `115200`
- Frequency: `433000000` Hz
- Spreading factor: `7`
- Bandwidth: `125000` Hz
- Coding rate: `4/5` via `setCodingRate4(5)`
- Sync word: `0x12`
- Preamble: `8`
- SPI: SCK 18, MISO 19, MOSI 23, NSS 5
- Control pins: RST 14, DIO0 26

## Safety

The firmware is receive-only. It calls `LoRa.begin()` for initialization and `LoRa.parsePacket()` for nonblocking polling, but it does not call `beginPacket()`, `endPacket()`, `write()`, or any transmit method. It sends no ACK and does not participate in SCF forwarding.

If initialization fails, the diagnostic prints a clear failure message and retries from `loop()` after a delay. It never claims packet reception until initialization succeeds.

## Build and upload

From PowerShell at the repository root:

```powershell
Set-Location D:\AutoLink-RadioSense
$pio = "C:\Users\chand\.platformio\penv\Scripts\pio.exe"
& $pio run -d test/embedded/lora_receiver -e lora_receiver
```

Upload only after reviewing the build:

```powershell
& $pio run -d test/embedded/lora_receiver -e lora_receiver -t upload --upload-port COM4
```

Monitor at 115200 baud:

```powershell
& $pio device monitor --port COM4 --baud 115200
```

The monitor prints an obvious `RECEIVED REAL RF PACKET` banner, packet number/length, escaped printable payload, hexadecimal payload bytes, RSSI, and SNR for each received packet.

This diagnostic requires a separate compatible LoRa transmitter to produce packets. It does not validate DTN forwarding, ACK/NACK behavior, multi-node delivery, RF range, or research performance.
