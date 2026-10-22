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
| Conflict | Deterministic canonical record selection and terminal protection |
| Memory | Priority admission, retained terminal history, compact replay protection and atomic reclamation |
| Retention | Read-only deadlines, cleanup capacity accounting and permanent terminal protection diagnostics |
| Sync | LoRa summaries, bounded requests/retries and persistent record/evidence imports |
| Console | Shared command parser |
| `Core/LoRa_Transport` | One radio owner with packet dispatch and asynchronous transmission |
| `Core/WiFi_Console` | Independent hotspot and bounded HTTP data API |

Capacity is 16 raw records and 32 observations per node. Snapshot schema 4 reads schemas 1–3 without inventing evidence or retirement markers. LoRa synchronization uses protocol version 5; upgrade all communicating nodes together. Protocol 5 shares compact retirement markers; older peers cannot safely participate. Existing records/evidence migrate on the next successful save. Observer IDs and report keys provide identity/deduplication, not authentication.

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
CLEANUP
RETENTION
RETIRED
POSITION <phone_lat> <phone_lon>
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

## Record conflict policy

`Hazard_Conflict` selects one complete canonical record for the same immutable ID, origin and creation time. It does not synthesize a version or combine individual fields. The clock-independent order is:

1. Resolved beats Expired; either terminal marker beats every ongoing revision.
2. Within the same terminal class, later `lastObservedAt` wins, then higher version.
3. Equal freshness/version uses stored state, then updating node ID (larger wins).
4. Remaining ties use explicit type, severity, coordinates, fix timestamp, accuracy, expiry and baseline confidence fields in that order (larger wins). These are mechanical tie-breakers, not evidence weights.

This total ordering makes repeated imports idempotent and yields the same winner regardless of arrival order. A fresher revision can replace a higher-version branch with an older observation; terminal protection can select an older/lower-version marker. Versions are branch revision counts, not a global logical clock. Resolved beats Expired because explicit clearance is retained over automatic timeout. A terminal ID cannot be reopened; a genuinely new occurrence needs a new report ID. Creation-time mismatches remain identity conflicts.

Synchronization requests every differing advertised version/digest, including equal and lower versions. Responses must match the advertised version/digest and correlated session/request; a record that changes during transfer is discovered through later summaries. The winner is saved and verified before becoming visible. Losing records cause no write, and immutable evidence from every branch still synchronizes independently. Summary digest collisions retain the existing CRC32 limitation. Conflict counters count detected differences, including differences later reconciled.

`MEMORY` exposes `conflict_policy=1`, `memory_policy=1` and `sync_protocol=5`. Upgrade all nodes together before testing. Selection does not count relay copies or confirmation votes as canonical revisions: independent confirmations/contradictions remain in the confidence engine, and cannot automatically reopen a cleared hazard. There is no authentication or verification that a remote terminal marker represents a real clearance.

## Memory policy

Fixed limits are 16 full records, 32 observations and 64 compact terminal identities. Capacity is checked before ID reservation. With known application UTC, admission can replace a strictly lower-priority ongoing record and reclaim its linked evidence. Equal-priority arrivals do not churn storage. Local copies may legitimately hold different cache subsets under pressure. Updating an existing ID still uses the shared conflict policy.

```text
priority = severity × 100 + calculated confidence × 2 + freshness + relevance
freshness = floor(100 × remaining TTL / full TTL), bounded to 0–100
relevance = floor((5000 − distance metres) / 50) within 5 km, otherwise 0
```

Relevance defaults to 0. `POSITION` accepts optional application-supplied coordinates used as a local priority focus until reboot or another POSITION command; it does not request GPS, set UTC, persist, or synchronize the focus. Applications should update it as their context changes. Deterministic victim ties prefer the oldest observation, then the larger ID. Terminal records and local keyed observations within the 300-second report acceptance window are pinned. Unknown UTC disables pressure eviction and cleanup. Only observations linked to a chosen victim are removed; unrelated orphan evidence is not silently discarded. When no safe victim exists, admission returns the existing full-capacity result without reserving an ID or writing flash.

Full Resolved/Expired records and linked evidence remain through `expiresAt + 86400 seconds`. Cleanup then replaces them with a 13-byte serialized terminal identity (ID, creation UTC, terminal state). The 64-entry marker table is fixed-size RAM/storage; retired IDs reject records, revisions, votes and evidence even after reboot with unknown UTC. Resolved dominates Expired in marker merges. Marker pages repeat over LoRa with rotated page boundaries, and receiving one atomically retires any matching local record/evidence. This also removes stale copies from reconnecting nodes. A peer's earlier terminal retirement can shorten another node's local full-history retention; identity mismatches are rejected. Retirement pages are unauthenticated like the rest of this prototype protocol.

Markers have no automatic expiry or overwrite policy. When all 64 slots are occupied, further terminal reclamation is deferred and full terminal history stays pinned; `retirement_full=1` reports this condition. This deliberately bounds memory while retaining replay protection. Unbounded deployments still need a reviewed archival/epoch strategy. Ongoing cache evictions are not globally retired and can be admitted again if capacity/priority later permits. Keyed idempotency lasts while evidence is retained; pinned local observations prevent duplicate creation during the accepted report window, and older evicted payloads fail normal age validation.

`CLEANUP` runs the same retention policy explicitly. Normal lifecycle processing batches eligible cleanup with state transitions, and admission batches cleanup, eviction and the accepted mutation into one verified snapshot. Identical commits, repeated cleanup, marker retransmissions and rejected admissions do not write snapshots. Accepted reports/evidence still persist before acknowledgement; there is no unsafe delayed-write buffer. `MEMORY` exposes capacities, `retired_count`, `retirement_full`, retention seconds, per-boot `evicted`/`cleaned` counts and optional position presence. GET/LIST include a local derived `priority`; RETIRED lists markers. The maximum snapshot is 3738 bytes; snapshots without markers retain the original record/evidence layout and schema 4 appends a count plus 13 bytes per marker only when needed.

## Terminal-marker retention

`Hazard_Retention` is the shared source of truth for full-history retention timing and cleanup eligibility. `RETENTION` reads its status without running cleanup, setting UTC or changing records. It reports `retention_policy=1`, current node UTC, full terminal histories, eligible histories, available marker slots, cleanup-ready/blocked counts and per-boot blocked record/evidence imports. Each full record includes its phase, terminal state, expiry, 64-bit cleanup deadline and remaining seconds. Unknown UTC is explicit; deadlines past the end of the 32-bit UTC range do not wrap into an early cleanup.

Phases are Ongoing, UnknownTime, HistoryRetained, Eligible and CapacityBlocked. If several histories are eligible but fewer marker slots remain, aggregate cleanup-ready/blocked counts reflect that partial capacity. Evaluating the view does not write storage. `CLEANUP` performs its own cleanup transaction without a preceding command-triggered lifecycle pass consuming the reported removals; scheduled lifecycle maintenance still runs normally.

Compact marker expiry is NEVER: no elapsed time or incoming ongoing revision can reopen a retired ID. A valid later Resolved record with matching ID/creation time can promote an Expired marker to Resolved, including when all 64 marker slots are occupied. The marker update persists before acceptance and never restores raw history or evidence. Synchronization continues fetching advertised records for an Expired archived ID so it can learn such a clearance; Resolved markers already dominate every record class. Marker gossip then shares the final terminal identity with reconnecting nodes. A mismatched creation time remains a conflict.

The demo's retention panel compares terminal ID/creation/state protection across full records and compact markers. Their storage representations may differ legitimately. Explicit cleanup preserves compact markers, and no automatic marker deletion control is provided. Diagnostics are additive: wire protocol stays 5 and snapshot schema stays 4; all retirement markers remain readable without migration. The fixed 64-marker limit still requires a separate archival/epoch design for unbounded operation.

## Independent laptop links

Each laptop runs a local Python application on http://127.0.0.1:8765 and connects to its own ESP32 hotspot. The ESP32 exposes only its data API at http://192.168.4.1. The identical node addresses belong to separate networks; LoRa handles peer synchronization. A router is unnecessary.

The SSID is `RadioSense-<node_id>` and the password is `HAZARD_WIFI_PASSWORD` (default `RadioSense32`). `HAZARD_WIFI_MAX_CLIENTS` defaults to one connected client; this is not permanent device pairing. AP-only mode does not load or overwrite old router credentials. Keep the hotspot connection if the laptop reports no internet.

GET `/` returns API metadata, not an HTML application. GET `/api/status` supplies node identity plus MEMORY/SYNC/LIST/GROUPS output; GET `/api/network` describes AP-only mode, `api_version=3`. POST `/api/command` requires `X-RadioSense: 1` and one `command` form field. Clients should also send `X-RadioSense-Node: <expected_node_id>`: mismatched targets receive 409 before execution. Commands are limited to 191 bytes, output to 16384 bytes. POST `/api/network` returns 409; router setup is disabled. Node IDs and hotspot access do not authenticate observations.

The local demo application and its map/browser assets remain outside the repository. The local proxy checks the selected node's identity before commands and sends the target-node header. Browser location on localhost requires permission; precision depends on the laptop's location provider. The map shows reported accuracy. Offline markers/grid are bundled; optional OpenStreetMap street tiles need internet. No fabricated or IP-based position is labelled as measured GPS.

### Controlled fresh-demo reset

`DEMO_RESET <own_node_id>` is accepted **only over USB Serial**. It clears raw records, immutable observations and retirement markers by writing and verifying an empty snapshot to both slots. It retains the NVS node identity and monotonic ID sequence, and persists a LoRa synchronization hold before erasure. Normal reports/assessments are blocked while held. Failed erasure does not acknowledge success; retry over USB, including after a reboot, until both slots are cleared.

Reset every participating board before resuming any. `DEMO_RESUME <own_node_id>` clears a completed hold and starts a fresh synchronization session; it may be sent through the node-bound local app. MEMORY exposes `demo_sync_paused`. The hold survives power loss, preventing an old peer from immediately refilling the cleared node. Other unreset peers must remain off during this clean demonstration because the reset deliberately removes prior replay protection. This maintenance reset is separate from normal CLEANUP, which preserves compact terminal identities.

Set `HAZARD_ENABLE_WIFI_CONSOLE=0` to disable the HTTP API. Application uploads normally retain hazard storage; the explicit reset command is required for erasure. Do not use uploadfs or erase unrelated NVS settings.

## Current limitations

Confidence weights are provisional and observer identities are unauthenticated. The mobile application, long-run hardware measurements and an archival strategy beyond the fixed retirement-marker capacity remain unfinished. Conflict selection uses deterministic prototype rules; it does not establish which physical report is true. Candidate groups preserve independent records instead of destructively merging their history.
