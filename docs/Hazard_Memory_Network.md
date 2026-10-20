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
| Confidence | Derived presence score, latest independent observer votes and time decay |
| Sync | LoRa summaries, bounded requests/retries and persistent record/evidence imports |
| Console | Shared command parser |
| `Core/LoRa_Transport` | One radio owner with packet dispatch and asynchronous transmission |
| `Core/WiFi_Console` | Optional HTTP monitor and router setup interface |

Capacity is 16 raw records and 32 observations per node. Snapshot schema 3 reads schemas 1 and 2 without inventing evidence. LoRa synchronization uses protocol version 3; upgrade all communicating nodes together. Older firmware cannot interpret the new confirmation/contradiction events. Observer IDs and report keys provide identity/deduplication, not authentication.

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
CONFIRM_ID <client_key> <hazard_id> <same report fields>
CONTRADICT_ID <client_key> <hazard_id> <same report fields>
CONFIDENCE <hazard_id>
GET <hazard_id>
EVIDENCE <hazard_id>
LIST
GROUPS
MEMORY
SYNC
HELP
```

Use a nonzero unsigned 64-bit client key, unique per observation on a node. An identical retry reuses that key and payload and returns UNCHANGED. Different content under the same key is a conflict. Types 1–5 are obstacle, damage, flooding, accident and other; severity is 1–5. Reports require known UTC, age no more than 300 seconds, fix age no more than 120 seconds, and reported accuracy within 100 metres. The service owns IDs, versions, states and confidence.

## Confidence policy

Confidence is a prototype score for hazard presence, not a calibrated probability. Weights live in `Hazard_Confidence.h` and need experimental calibration:

```text
evidence score = clamp(50 + 20 × max(supporting observers − 1, 0)
                         − 25 × contradicting observers, 0, 100)
current score = max(evidence score − 10 × elapsed TTL quarters, 0)
```

Resolved/Expired hazards have current presence score 0. Fresh hazards with score at least 70 have effective Active state; lower scores are Unverified. Half-TTL staleness and terminal protections still apply regardless of score. Low scores alone do not clear or expire a record.

Each node ID contributes one latest vote. A newer vote replaces its prior stance; contradictions win equal timestamps, then event ID breaks equivalent ties. Repeating confirmations on one node, retrying a client key, and relaying evidence cannot add independent votes. Node IDs are not authenticated proof of separate vehicles.

Eligible Report/Revision events come from the same deterministic candidate group and must match the current type/location within 25 m, have accuracy within 25 m and a fix no more than 120 seconds old, and be within 120 seconds of the record's last observation. Confirmation/Contradiction events may arrive later, but must not predate that observation or fall outside its validity period. Local assessments require known UTC, the same hazard type, a fresh fix within 25 m and accuracy within 25 m; terminal records reject new assessments. Controlled clearances on matching group members count as negative evidence and retain their original fix timestamp. Future-dated events are excluded when UTC is known. Legacy records without evidence retain the initial score, with zero evidenced supporters.

Assessments append immutable events (kinds 4 Confirmation and 5 Contradiction), commit before acknowledgment, and synchronize through the evidence protocol. They do not rewrite hazard location, severity, expiry or version. A contradiction affects presence confidence; explicit clearance remains a separate action.

Scores and effective state are computed from persisted evidence plus local UTC. The canonical `confidence` field remains its stored compatibility baseline; GET/LIST/CONFIDENCE additionally return `effective_confidence`, `evidence_score`, `decay_penalty`, `supporting_observers`, `contradicting_observers`, `confidence_level` and `confidence_time_known`. Use `effective` for the current state; `status` is the canonical persisted lifecycle state. This separates clock-dependent views from shared records, prevents confidence-only version conflicts, and avoids periodic flash writes. Peers with the same evidence and UTC derive the same score. Missing evidence or different clocks can legitimately produce different local scores.

After reboot, `confidence_time_known=0` reports an evidence-only score without age decay and `effective=UnknownTime` for ongoing hazards. Supply UTC before treating such a score as current. Persisted resolved/expired markers remain terminal without UTC. Computing confidence never writes snapshots; command handling and normal lifecycle processing still persist stale/expired transitions when needed.

## Optional Wi-Fi interface

The board starts `RadioSense-<node_id>` with the password configured by `HAZARD_WIFI_PASSWORD` (default `RadioSense32`). Its setup page is http://192.168.4.1. Enter a 2.4 GHz router's credentials on that page to join a shared LAN; the page and serial output show the assigned router IP. The setup hotspot remains available, and credentials are stored in separate NVS settings. The router must allow communication between clients. Router LAN subnet 192.168.4.x can conflict with the setup hotspot subnet.

Endpoints: GET `/`, GET `/api/status`, GET `/api/network`, POST `/api/command`, POST `/api/network`. POST requests require `X-RadioSense: 1`; commands use a `command` form field. The interface bounds command length to 191 bytes and output to 16384 bytes. Network settings are submitted as `ssid` and `password` form fields; the password is not returned. Anyone with network access to the interface can issue modifying commands, so it is intended for a private research setup.

Set `HAZARD_ENABLE_WIFI_CONSOLE=0` to disable HTTP monitoring. LoRa remains the synchronization transport. Keep one owner of the radio; other components should use the shared transport rather than independently consuming LoRa packets.

## Current limitations

Confidence weights are provisional and observer identities are unauthenticated. Automatic canonical-record conflict resolution, retention/eviction policies and the mobile application remain unfinished. Equal-version record differences are detected rather than automatically reconciled; lifecycle transitions made independently on two nodes can therefore expose a conflict. Candidate groups preserve independent records instead of destructively merging their history.
