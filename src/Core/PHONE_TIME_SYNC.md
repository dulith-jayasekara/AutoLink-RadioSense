# Phone time synchronization

The HTTPS captive-portal page sends `Date.now()` once when it loads. The value
is Unix time in milliseconds; the ESP32 validates it and uses `settimeofday()`
to set the system clock. A failed request exposes a retry button, but there is
no background polling and no extra phone permission is requested.

The prototype accepts timestamps from 2020-01-01 through 2100-01-01 UTC. Adjust
`MIN_PHONE_TIMESTAMP_MS` and `MAX_PHONE_TIMESTAMP_MS` in `Time_Manager.cpp` if
the deployment needs a different range. The browser clock is trusted as the
time source, so a phone with an incorrect clock can set an incorrect ESP32
clock.

After synchronization, the accepted phone time is kept as a timestamp base.
Whenever a beacon is generated, the ESP32 advances that stored value by the
elapsed `millis()` time and places its current whole Unix seconds in the
packet. Fractional milliseconds are retained between updates, so the timestamp
does not lose time by rounding each update. The existing four-byte timestamp
field is unchanged. A value of zero means the sender has not synchronized its
clock during this boot. Time is marked unsynchronized after every restart or
power loss; without an RTC or network time source it must be synchronized from
a phone again.

## Check with two nodes

1. Build and upload the production firmware to both nodes. Install/trust the
   portal CA on the phone as described in `PORTAL_TLS_SETUP.md`.
2. Join the first node's Wi-Fi from the phone and open
   `https://192.168.4.1/` in the regular browser. The page should report that
   the vehicle clock synchronized.
3. Check that node's Serial Monitor for `System clock synchronized to Unix
   seconds`, then wait for a beacon. Its transmit log should show a nonzero
   `Timestamp (Unix seconds)`.
4. Check the second node's receive log. It should decode the same timestamp
   from the first node's beacon. The second node's own transmit timestamp stays
   zero until a phone also loads its trusted portal page.
5. Disconnect the phone. The synchronized node continues using its local
   system clock. Restart it and its beacon timestamp returns to zero until the
   phone synchronizes it again.

Captive-portal auto-opening depends on the phone operating system. If it does
not open automatically, open the portal URL manually after joining the access
point.
