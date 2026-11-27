# GPS Data Validation, Availability and Freshness Management

**Project:** AutoLink RadioSense  
**Version:** 2.0 | Implementation design reference  
**Status:** Proposed implementation; numerical freshness parameters require experimental validation.

This version supersedes Version 1, including its fixed position timeout recommendation. It incorporates the agreed 100 km/h operational speed cap and the hybrid adaptive expiration methodology.

## 1. Purpose and scope

Validate phone-derived GPS measurements received over Wi-Fi before use in beacon construction, adaptive beacon scheduling and geographic routing. Retain usable measurements through short interruptions and invalidate them after their respective deadlines. Continuous acquisition and adaptive beacon transmission are reported as already implemented; this document does not claim the new mechanisms have been implemented or tested.

## 2. Architecture and file responsibilities

```text
GPS Management/
├── GPS_Manager.h
├── GPS_Manager.cpp
└── GPS_Validation.cpp
```

- `GPS_Manager.h`: shared GPS structures, state enums and function prototypes. Do not create `GPS_Validation.h`.
- `GPS_Manager.cpp`: coordinate processing, snapshot storage, timeout updates, retrieval and diagnostics.
- `GPS_Validation.cpp`: parsing, field validation, speed clamping, timestamp checks, adaptive threshold calculation and freshness classification.
- `Config.h`: configurable parameters; shared sentinel definitions in an existing header accessible to sender and receiver.
- Time Management: reuse existing timestamp parsing and clock synchronization utilities.
- Beacon module: serialize validated operational values or sentinels; preserve existing adaptive beacon algorithm and 25-byte wire format.

No GPS status flags or additional fields shall be transmitted. Internal availability and freshness states are permitted.

## 3. GPS input validation

| Field | Validation and handling |
|---|---|
| Latitude | Required, finite, −90 to +90 degrees, inclusive. |
| Longitude | Required, finite, −180 to +180 degrees, inclusive. |
| Accuracy | Required by current interface; finite, nonnegative, within configurable acceptable error threshold. |
| Speed | Optional, finite, nonnegative; clamp above 100 km/h. |
| Heading | Optional, finite, 0 inclusive to 360 exclusive degrees. |
| Fix timestamp | Required, plausible Unix milliseconds, representable in wire seconds, not duplicate, out of order, excessively future-dated or already expired. |

Reject malformed, partially parsed, overflowed and nonfinite numeric inputs. Missing, empty and `null` optional fields are unavailable. Never silently convert invalid inputs to zero. Latitude and longitude form an indivisible position pair. Reject a new snapshot if either coordinate, required accuracy or timestamp fails validation; retain the previous snapshot only until its existing deadlines. Invalid optional speed or heading does not reject a valid position; mark that field unavailable in the new snapshot. Do not carry old motion fields into a newer snapshot.

The existing 1 m accuracy target is a diagnostic, not necessarily a rejection threshold. The maximum acceptable accuracy remains an experimental parameter. Optional displacement plausibility checks must account for measurement uncertainty and must not be treated as proven physical bounds.

### 3.1 Speed validation and clamping

Phone speed is in m/s. Accept zero; reject negative or nonfinite values. Clamp any otherwise valid finite speed above 100 km/h to the operational cap, 100/3.6 m/s. Keep the original input in diagnostics where useful. The clamped value is used for beacon encoding, adaptive beacon scheduling and position timeout calculations. It is not a claim that actual vehicle speed cannot exceed 100 km/h.

```cpp
bool GPS_ValidateSpeed(double &speedMps) {
    if (!isfinite(speedMps) || speedMps < 0.0) return false;
    if (speedMps > GPS_MAX_SPEED_MPS) speedMps = GPS_MAX_SPEED_MPS;
    return true;
}
```

With nearest-integer encoding, maximum speed is 2778 cm/s. Verify the existing rounding convention before integration. Check the encoded range before casting. Heading rounding near 360 degrees must wrap consistently with the existing whole-degree or tenths-of-degree encoding; inspect current sender and receiver rather than changing the convention.

## 4. Sentinel values and wire format

| Field | Type | Sentinel | Decimal |
|---|---|---|---:|
| Latitude | int32_t | INT32_MIN | -2147483648 |
| Longitude | int32_t | INT32_MIN | -2147483648 |
| Speed | uint16_t | UINT16_MAX | 65535 |
| Heading | uint16_t | UINT16_MAX | 65535 |
| Timestamp | uint32_t | UINT32_MAX | 4294967295 |

```cpp
#include <stdint.h>
constexpr int32_t GPS_LATITUDE_UNAVAILABLE = INT32_MIN;
constexpr int32_t GPS_LONGITUDE_UNAVAILABLE = INT32_MIN;
constexpr uint16_t GPS_SPEED_UNAVAILABLE = UINT16_MAX;
constexpr uint16_t GPS_HEADING_UNAVAILABLE = UINT16_MAX;
constexpr uint32_t GPS_TIMESTAMP_UNAVAILABLE = UINT32_MAX;
```

Zero is valid for coordinates, stationary speed and north heading. Keep internal floating-point values separate from wire sentinels. Define constants once. Preserve the existing serialized layout: packet type 1 byte, node ID 6, sequence 2, latitude 4, longitude 4, speed 2, heading 2, timestamp 4 = 25 bytes. Preserve byte order and serialization; native struct padding is not wire size.

## 5. Coherent snapshot storage

Store validated coordinates, accuracy, optional speed and heading, original fix timestamp (Unix ms), monotonic reception time, initial measurement age, individual availability/state and the calculated position freshness/expiration thresholds. Store the last accepted timestamp independently of operational availability to reject replayed fixes after expiration.

All operational fields in a beacon must originate from the same snapshot. Independent expiration removes fields from that snapshot; it never mixes measurement times. A newly accepted snapshot replaces the old one, including unavailable optional fields. Keep thresholds frozen for the lifetime of the snapshot. If callbacks and beacon generation run concurrently, protect snapshot updates and reads using existing synchronization conventions.

## 6. Internal GPS states

```cpp
enum GPS_FreshnessState {
    GPS_FRESH, GPS_STALE, GPS_EXPIRED, GPS_UNAVAILABLE
};
```

| State | Condition | Wire behaviour |
|---|---|---|
| FRESH | Valid field, age < freshness threshold | Encode value |
| STALE | Valid field, freshness threshold <= age < expiration threshold | Retain value and original timestamp |
| EXPIRED | Previously valid field, age >= expiration threshold | Encode sentinel |
| UNAVAILABLE | No valid field in current snapshot or no accepted snapshot | Encode sentinel |

EXPIRED and UNAVAILABLE are distinct internally but indistinguishable on the wire. Expiration is latched until a newer valid snapshot arrives. Latitude, longitude and accuracy share one state; speed and heading have independent states. A delayed but acceptable fix may begin STALE rather than FRESH. An already expired position must not be accepted as a new snapshot; optional motion fields already past their deadlines are expired immediately.

## 7. Hybrid adaptive expiration methodology

Position uncertainty grows with displacement, whereas speed changes depend on acceleration and heading changes depend on turning. Use speed-adaptive position thresholds and short fixed independent motion thresholds initially.

| Datum | Method |
|---|---|
| Latitude and longitude | Shared speed-adaptive thresholds |
| Accuracy | Same lifetime as position |
| Speed | 1 s freshness; 3 s expiration, or 10 s for a valid zero-speed reading |
| Heading | Fixed 1 s freshness / 2 s expiration |
| Timestamp | Original fix time, retained while any associated field remains operational |

### 7.1 Adaptive position threshold calculation

Under a constant-speed approximation, displacement d = v × t. Use v in m/s and distances in metres. The proposed freshness displacement is 25 m and expiration displacement is 50 m.

For v > 0:

```text
T_fresh_ms  = clamp(1000 × 25 / v, 500, 3000)
T_expire_ms = clamp(1000 × 50 / v, 1500, 10000)
Grace_ms    = T_expire_ms - T_fresh_ms
```

For valid zero speed, use 3000 ms freshness and 10000 ms expiration. For missing or invalid speed, use the maximum operational speed (100/3.6 m/s) solely as the threshold calculation input. Never broadcast this fallback as measured speed. Clamp valid speeds before calculating thresholds. Compute and freeze thresholds when accepting a snapshot; do not recalculate when speed later expires. Clamp floating-point results before converting to integer milliseconds (nearest millisecond for this design). Handle very small positive speeds safely. Verify freshness < expiration for every supported configuration.

| Speed (km/h) | Freshness (s) | Expiration (s) | Grace (s) |
|---:|---:|---:|---:|
| 0 | 3.000 | 10.000 | 7.000 |
| 5 | 3.000 | 10.000 | 7.000 |
| 10 | 3.000 | 10.000 | 7.000 |
| 20 | 3.000 | 9.000 | 6.000 |
| 30 | 3.000 | 6.000 | 3.000 |
| 40 | 2.250 | 4.500 | 2.250 |
| 50 | 1.800 | 3.600 | 1.800 |
| 60 | 1.500 | 3.000 | 1.500 |
| 70 | 1.286 | 2.571 | 1.285 |
| 80 | 1.125 | 2.250 | 1.125 |
| 90 | 1.000 | 2.000 | 1.000 |
| 100 / unavailable fallback | 0.900 | 1.800 | 0.900 |

### 7.2 Speed and heading expiration

Nonzero speed: fresh below 1000 ms, stale from 1000 through 2999 ms, expired at 3000 ms. A valid zero-speed reading remains stale from 1000 ms until it expires at 10000 ms. Heading: fresh below 1000 ms, stale from 1000 through 1999 ms, expired at 2000 ms. Zero speed uses the longer expiry so delayed stationary readings can still be transmitted; it may temporarily remain zero if the vehicle starts moving and fresh updates stop. Acceleration-based speed and angular-rate-based heading thresholds are future research, not current implementation requirements.

## 8. Configuration parameters

```cpp
constexpr double GPS_MIN_SPEED_MPS = 0.0;
constexpr double GPS_MAX_SPEED_KMPH = 100.0;
constexpr double GPS_MAX_SPEED_MPS = GPS_MAX_SPEED_KMPH / 3.6;
constexpr double GPS_POSITION_FRESH_DISTANCE_M = 25.0;
constexpr double GPS_POSITION_EXPIRE_DISTANCE_M = 50.0;
constexpr uint32_t GPS_POSITION_MIN_FRESH_MS = 500;
constexpr uint32_t GPS_POSITION_MAX_FRESH_MS = 3000;
constexpr uint32_t GPS_POSITION_MIN_EXPIRE_MS = 1500;
constexpr uint32_t GPS_POSITION_MAX_EXPIRE_MS = 10000;
constexpr uint32_t GPS_SPEED_FRESH_MS = 1000;
constexpr uint32_t GPS_SPEED_EXPIRE_MS = 3000;
constexpr uint32_t GPS_ZERO_SPEED_EXPIRE_MS = 10000;
constexpr uint32_t GPS_HEADING_FRESH_MS = 1000;
constexpr uint32_t GPS_HEADING_EXPIRE_MS = 2000;
```

The speed cap is an agreed operational policy, not a verified universal legal limit. All distance and time thresholds are provisional. Maximum acceptable accuracy, timestamp future tolerance and adaptive beacon fallback interval remain to be determined; reuse existing settings where appropriate and document any provisional defaults. Validate positive distances, ordered min/max limits and fresh/expire ordering, with compile-time checks where practical. Test the entire speed domain when changing parameters.

## 9. Timestamp and measurement age

Reuse `Time_ParsePhoneTimestamp()` and `Time_IsTimestampReasonable()` after inspecting their contracts. Compare full Unix millisecond timestamps, reject duplicates and older fixes, and ensure floor(timestampMs / 1000) is representable without colliding with UINT32_MAX. Preserve original timestamp, never replace it with transmission time.

With a trustworthy synchronized clock, calculate initial age at reception as max(0, reception Unix ms − fix Unix ms), rejecting excessive future offsets according to a configured tolerance. Then use monotonic elapsed time:

```text
age_ms = initial_age_ms + uint32_t(now_millis - reception_millis)
```

Use wide arithmetic for the sum. Do not subtract Unix time from millis(). If clock synchronization is unavailable, use monotonic reception age as a fallback, explicitly acknowledging that cached/delayed fix age cannot be verified. If clock becomes trustworthy later, it may shorten remaining lifetime but must not revive expired data or extend deadlines. Handle clock resets explicitly; do not silently accept old fixes. Check expiry regularly so unsigned timer rollover cannot revive a snapshot. Duplicate, rejected and retransmitted measurements never reset age.

## 10. Processing, timeout and recovery

1. Parse required and optional inputs independently.
2. Validate coordinate pair, accuracy and timestamp; reject invalid required data without replacing the existing snapshot.
3. Validate and clamp speed; validate heading; mark invalid optional fields unavailable.
4. Calculate and freeze position thresholds using validated speed or fallback.
5. Determine initial age; reject already expired position; classify optional fields by their own deadlines.
6. Atomically store the accepted snapshot and its original timestamp.
7. Run `GPS_UpdateFreshness()` regularly even when no phone requests arrive, and check freshness before every operational getter.
8. Transition each available field from fresh to stale to expired according to age; latch expiry and exclude expired values from operational use.
9. Automatically recover on a newer valid measurement. Keep expired values only for diagnostics if desired.

Suggested public interfaces in `GPS_Manager.h`: existing `GPS_ProcessLocation`, `GPS_GetLocation`, `GPS_GetVehicleMotion`, plus `GPS_UpdateFreshness` and a coherent snapshot getter. Validation prototypes belong in the same header. Ensure getters report availability and never expose expired values as valid zero. Preserve existing caller contracts where possible and update affected callers.

## 11. Beacon integration and worked examples

Encode fresh and stale values normally. Encode expired/unavailable fields with their respective sentinels. Position is all-or-nothing. Keep the original timestamp while **any** associated position, speed or heading remains operational, even if position expires first. Once all are unavailable/expired, transmit the timestamp sentinel. Continue transmitting node ID and sequence number regardless of GPS availability.

Example: 40 km/h, latitude 6.9271, longitude 79.8612, heading 90 degrees. Position thresholds 2250/4500 ms, speed 1000/3000 ms, heading 1000/2000 ms. Assume whole-degree heading and nearest-integer speed encoding.

| Age (ms) | Position | Speed | Heading | Wire lat / lon | Wire speed | Wire heading | Timestamp |
|---:|---|---|---|---|---:|---:|---|
| 0 | Fresh | Fresh | Fresh | 69271000 / 798612000 | 1111 | 90 | Original |
| 1000 | Fresh | Stale | Stale | same | 1111 | 90 | Original |
| 2000 | Fresh | Stale | Expired | same | 1111 | 65535 | Original |
| 2250 | Stale | Stale | Expired | same | 1111 | 65535 | Original |
| 3000 | Stale | Expired | Expired | same | 65535 | 65535 | Original |
| 4500 | Expired | Expired | Expired | both INT32_MIN | 65535 | 65535 | UINT32_MAX |

At 100 km/h, position expires at 1800 ms, heading at 2000 ms, speed at 3000 ms. Between 1800 and 3000 ms the original timestamp remains available with the remaining motion data. At 3000 ms all GPS fields are sentinels. This corrects the earlier position-only timestamp lifetime rule.

## 12. Adaptive beacon scheduling

Preserve the existing adaptive beacon interval algorithm. Supply validated clamped speed while fresh or stale within its own deadline, regardless of position availability. When speed is unavailable or expired, use a configurable fallback interval; never feed the speed sentinel or substitute zero. The fallback speed used for position threshold calculation is unrelated to the fallback beacon interval. Do not change LoRa parameters or suppress beacons merely because GPS is unavailable.

## 13. Receiver and neighbour management

Check sentinels before coordinate decoding, displacement calculations, heading comparisons, speed conversion or timestamp interpretation. Invalidate operational neighbour fields when a newer beacon reports their sentinels. Continue processing node ID, sequence, RSSI, SNR, PDR and last-seen independently. A repeated beacon does not refresh the original GPS measurement age. Reject older GPS snapshots from replacing newer ones where ordering is available; respect existing packet sequence handling.

With synchronized clocks, estimate age from the original Unix seconds timestamp. Account for up to roughly one second of timestamp quantization plus clock uncertainty. The receiver cannot reconstruct exact sender freshness states, especially when speed is absent or has expired and the sender's frozen position thresholds are unknown. Use conservative receiver-side routing eligibility policies; do not infer precise sender deadlines or treat missing coordinates as zero. Without trustworthy time, receiver-side age is only a limited estimate. A stale value is not necessarily suitable for routing merely because it remains eligible for transmission.

## 14. Experimental limitations and threshold determination

The constant-speed displacement model is a heuristic, not a guaranteed error bound. Acceleration, braking, turning, GPS uncertainty, browser update latency, Wi-Fi delays and speeds above the cap can increase actual error. A stationary vehicle can start moving immediately. The minimum time clamps can also exceed displacement targets under different parameter choices. Measure actual phone fix intervals and delivery delays, positioning accuracy, vehicle speeds and routing sensitivity. Evaluate stationary, slow, urban and high-speed conditions, including interruptions. Tune distance/time thresholds and fallback interval based on measured results; do not present the defaults as validated production settings.

## 15. Implementation checklist

- [ ] Create GPS_Validation.cpp and move parsing/validation functions; keep prototypes in GPS_Manager.h.
- [ ] Configure speed cap, adaptive thresholds, independent motion thresholds and shared sentinels.
- [ ] Implement coherent snapshot and timestamp ordering/age checks.
- [ ] Calculate and freeze adaptive thresholds on acceptance.
- [ ] Implement four internal states, timeout latching and recovery.
- [ ] Update getters, beacon serialization, adaptive scheduling and implemented receiver consumers.
- [ ] Add concise diagnostics for rejection, clamping, state transitions and recovery.
- [ ] Include the new source in relevant PlatformIO environments; preserve separate system tests and production main.cpp.
- [ ] Build affected environments and perform the tests below; distinguish simulated tests from hardware results.

## 16. Test plan

| Test | Expected result |
|---|---|
| Startup without fix | All GPS fields sentinel; beacon still transmitted |
| Zero coordinates, speed, heading | Valid when other required fields pass |
| Invalid coordinate or accuracy | Reject snapshot; previous age continues |
| Null/malformed/negative speed | New valid position accepted; speed unavailable; conservative position thresholds |
| 100 km/h and above | Accept/clamp; maximum encoded 2778 with nearest rounding |
| NaN/infinity/overflow | Reject affected field before casting |
| Heading near 360 | Valid encoding with correct wrap; 360 input rejected |
| Missing heading | Position accepted; heading sentinel |
| Duplicate/older timestamp | Reject; age unchanged, including after expiry |
| Delayed cached fix | Initial age included; reject expired position when verifiable |
| Future timestamp | Reject beyond configured tolerance |
| 0, 10, 40, 100 km/h | Match threshold table |
| Very small positive speed | Safe calculation, capped maximum thresholds |
| Exact threshold boundaries | Fresh-to-stale and stale-to-expired at specified boundaries |
| 40 km/h interruption | Heading expires 2 s, speed 3 s, position 4.5 s |
| 100 km/h interruption | Position expires 1.8 s, heading 2 s, speed 3 s; timestamp survives until 3 s |
| Speed expires before position | Frozen position deadline does not change |
| New snapshot with null speed | No old speed mixed with new timestamp |
| No incoming requests | Periodic timeout still operates |
| millis rollover | Correct elapsed age; expired data never revives |
| Clock adjustment | No age reset or lifetime extension |
| Recovery | New valid snapshot restores eligible fields |
| Receiver sentinel handling | No invalid geographic calculations; link statistics continue |
| Wire serialization | Exactly 25 bytes; no status flags |

## 17. Decisions and remaining work

Agreed: 25-byte wire format, no transmitted GPS status flags, sentinel values, 0–100 km/h operational speed with clamping, separate GPS_Validation.cpp and existing GPS_Manager.h. Proposed design: four internal states, hybrid position/motion expiration, frozen thresholds, coherent snapshot and timestamp retained until all associated fields expire. Provisional: all distance/time thresholds and fallback scheduling settings. Verify actual heading encoding, timestamp utility contracts, accuracy limit, receiver integration and PlatformIO filters against current code. No source code has been changed by this document.
