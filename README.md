# AutoLink RadioSense

ESP32/LoRa research firmware. This branch adds the distributed hazard memory subsystem and its reusable automated tests.

| Directory | Purpose |
| --- | --- |
| `src/Core/` | Shared radio transport and Wi-Fi interface |
| `src/Hazard Memory Network/` | Hazard records, evidence, storage, lifecycle and synchronization |
| `include/` | Shared configuration and headers |
| `lib/` | Bundled dependencies |
| `data/` | Initial LittleFS provisioning contents |
| `test/unit/`, `test/host_stubs/` | Reusable firmware tests and host fakes |
| `test/system/` | Existing raw-radio tests and live HTTP smoke checks |
| `docs/` | Shared project documentation |

## Build

```bash
pio run -e production
```

The existing `raw_tx`, `raw_rx`, and `raw_tx_rx` environments remain available. `hazard_serial` runs the hazard service with LoRa disabled.

## Test

```bash
python3 test/run_hazard_host_tests.py
```

Requires a C++11 compiler (`clang++` by default; override with `CXX`). These tests run without ESP32 hardware and use temporary storage.

For a live board's HTTP interface:

```bash
python3 test/system/wifi_console_smoke.py --url http://NODE_IP --expect-node NODE_ID
```

See [Hazard Memory Network integration](docs/Hazard_Memory_Network.md) for architecture, configuration, command formats and current limitations.

Personal experiment logs, stage notes, local dashboards and machine-specific settings belong outside this repository. They are not required to build or test the shared firmware.
