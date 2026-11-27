# Adaptive Beacon Interval Calculation

**Project:** AutoLink RadioSense\
**Subsystem:** Beacon Management\
**Mechanism:** Linear speed-based adaptive beacon transmission

## 1. Objective

Dynamically adjust the beacon transmission interval according to vehicle speed. Faster vehicles transmit more frequently to keep neighbour information updated, while slower and stationary vehicles transmit less frequently to reduce communication overhead.

## 2. Configuration Parameters

  ------------------------------------------------------------------------------------------
  Parameter                    Constant                                        Initial value
  ---------------------------- -------------------------------- ----------------------------
  Maximum interval             `BEACON_MAX_INTERVAL_MS`                            15,000 ms

  Minimum interval             `BEACON_MIN_INTERVAL_MS`                             2,000 ms

  Reference maximum speed      `BEACON_MAX_SPEED_KMPH`                              100 km/h

  Stationary speed threshold   `BEACON_STATIONARY_SPEED_KMPH`                       1.8 km/h

  Fallback interval            `BEACON_DEFAULT_INTERVAL_MS`                         5,000 ms
  ------------------------------------------------------------------------------------------

All values are provisional and will be refined through experimentation.

## 3. Calculation Logic

The vehicle speed is stored in centimetres per second (cm/s). Convert it to kilometres per hour (km/h):

`v = speed × 0.036`

Calculate the linear interval reduction coefficient:

`k = (T_max - T_min) / v_max`

Using the initial parameters:

`k = (15000 - 2000) / 100 = 130 ms per km/h`

For speeds between the stationary threshold and reference maximum speed, calculate the interval using:

`T(v) = T_max - k × v`

Therefore, the initial linear equation is:

`T(v) = 15000 - 130 × v`

## 4. Decision Logic

1.  If vehicle speed is unavailable, return the fallback interval (5,000 ms).
2.  Convert the available speed from cm/s to km/h.
3.  If speed is less than or equal to 1.8 km/h, return the maximum interval (15,000 ms).
4.  If speed is greater than or equal to 100 km/h, return the minimum interval (2,000 ms).
5.  Otherwise, calculate the interval using the linear equation and round it to the nearest millisecond.

## 5. Example Intervals

    Vehicle speed (km/h)   Beacon interval (ms)
  ---------------------- ----------------------
                       0                 15,000
                     1.8                 15,000
                      10                 13,700
                      20                 12,400
                      40                  9,800
                      60                  7,200
                      80                  4,600
                     100                  2,000
                     120                  2,000
             Unavailable                  5,000

These are theoretical values; integer speed encoding in cm/s may introduce small rounding differences.

## 6. Implementation

-   `calculateBeaconInterval(speed, speedAvailable)` calculates and returns the interval in milliseconds.
-   `handleBeaconTransmission(speed, speedAvailable)` recalculates the interval on each call and compares it against the elapsed time since the previous transmission attempt using `millis()`.
-   When the elapsed time reaches the calculated interval, a beacon is generated and transmitted, and the transmission timer is updated.
-   The scheduling mechanism does not use `delay()`. The existing LoRa transmission operation itself remains blocking.
-   The existing 25-byte beacon packet format remains unchanged.

## 7. Current Limitations and Future Work

-   Actual vehicle speed acquisition from the mobile phone is pending; temporary speed values can be used for functional testing.
-   The thresholds are not experimentally validated. Evaluate channel utilization, PDR, neighbour information freshness and hazard delivery performance before finalizing them.
-   The neighbour timeout must accommodate the maximum beacon interval and scheduling delays.
-   Speed smoothing and transmission jitter are not currently implemented.
