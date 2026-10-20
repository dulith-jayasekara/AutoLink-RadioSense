# Hazard Memory Network integration

The subsystem lives under `src/Hazard Memory Network/`. `src/main.cpp` coordinates the service, command input, periodic lifecycle processing and shared radio dispatcher. GPS coordinates and UTC are supplied by the application; there is no onboard GPS input in this implementation.

## Modules

| Module | Responsibility |
| --- | --- |
| Record / Repository | Validated records, fixed-capacity tables and queries |
| Service | Creation, revision, clearance, input validation and persistent ID allocation |
| Storage / LittleFS | Versioned CRC snapshots, alternating slots and verified commits |
| Lifecycle | Unknown-time handling, stale/expired states and terminal markers |
| Evidence / Groups | Immutable event history, keyed retries and non-destructive candidate groups |
| Sync | LoRa summaries, bounded requests/retries and persistent record/evidence imports |
| Console | Shared command parser |
| `Core/LoRa_Transport` | One radio owner with packet dispatch and asynchronous transmission |
| `Core/WiFi_Console` | Optional HTTP monitor and router setup interface |

Capacity is 16 raw records and 32 observations per node. Snapshot schema 2 can read schema 1. LoRa synchronization uses protocol version 2; communicating nodes must use compatible firmware. Observer IDs and report keys provide identity/deduplication, not authentication.

## Configuration and persistence

Radio pins and parameters are in `src/Core/LoRa_Manager.h`. Node identity and feature flags are in `include/Config.h`. The default node ID is derived from the factory MAC; explicit IDs must be unique. NVS reserves sequence numbers before records are acknowledged.

Production uses LittleFS and a 32 KiB loop stack. Application uploads retain hazard storage. `uploadfs` replaces the filesystem image and must not be used on a populated board unless an intentional reset is required. A mount/verification failure leaves record storage read only rather than auto-formatting it.

After reboot, UTC remains unknown until the application supplies TIME. Existing timestamps and terminal states are preserved. Clock advancement can make older records stale or expired.

## Application interface

Serial and Wi-Fi commands use the same parser and service:

```text
TIME <unix_seconds>
REPORT_ID <client_key> <type> <severity> <lat> <lon> <observed_utc> <fix_utc> <accuracy_m>
REVISE_ID <client_key> <hazard_id> <same report fields>
RESOLVE_ID <client_key> <hazard_id> <clearance_utc>
GET <hazard_id>
EVIDENCE <hazard_id>
LIST
GROUPS
MEMORY
SYNC
HELP
```

Use a nonzero unsigned 64-bit client key, unique per observation on a node. An identical retry reuses that key and payload and returns UNCHANGED. Different content under the same key is a conflict. Types 1–5 are obstacle, damage, flooding, accident and other; severity is 1–5. Reports require known UTC, age no more than 300 seconds, fix age no more than 120 seconds, and reported accuracy within 100 metres. The service owns IDs, versions, states and confidence.

## Optional Wi-Fi interface

The board starts `RadioSense-<node_id>` with the password configured by `HAZARD_WIFI_PASSWORD` (default `RadioSense32`). Its setup page is http://192.168.4.1. Enter a 2.4 GHz router's credentials on that page to join a shared LAN; the page and serial output show the assigned router IP. The setup hotspot remains available, and credentials are stored in separate NVS settings. The router must allow communication between clients. Router LAN subnet 192.168.4.x can conflict with the setup hotspot subnet.

Endpoints: GET `/`, GET `/api/status`, GET `/api/network`, POST `/api/command`, POST `/api/network`. POST requests require `X-RadioSense: 1`; commands use a `command` form field. The interface bounds command length to 191 bytes and output to 8192 bytes. Network settings are submitted as `ssid` and `password` form fields; the password is not returned. Anyone with network access to the interface can issue modifying commands, so it is intended for a private research setup.

Set `HAZARD_ENABLE_WIFI_CONSOLE=0` to disable HTTP monitoring. LoRa remains the synchronization transport. Keep one owner of the radio; other components should use the shared transport rather than independently consuming LoRa packets.

## Current limitations

Confidence starts at 50 and is preserved on replication. Confirmation/contradiction scoring, confidence decay, automatic conflict resolution, retention/eviction policies and the mobile application are unfinished. Equal-version differences are detected rather than automatically reconciled; lifecycle transitions made independently on two nodes can therefore expose a conflict. Candidate groups preserve independent records instead of destructively merging their history.
