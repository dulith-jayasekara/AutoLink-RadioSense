# RadioSense local laptop demo

Install Python 3.9+ on EACH laptop. No Python packages are required.

1. Extract this folder onto both laptops.
2. Laptop A: join `RadioSense-669563705` with password `RadioSense32`.
3. Laptop B: join `RadioSense-1747025710` with password `RadioSense32`.
4. On A run `python3 app.py --node a`; on B run `python3 app.py --node b`.
   Windows: use `py -3` instead of `python3`, or double-click the corresponding `.bat` file.
   macOS: the corresponding `.command` file is also supplied (OS permission prompts may apply).
5. Open http://127.0.0.1:8765 on EACH laptop. Keep Wi-Fi connected when it reports no internet.

Each Python server binds only to its own laptop's loopback interface and proxies its own node's API at 192.168.4.1. That identical ESP32 address belongs to separate hotspots. No application page is stored on the ESP32. LoRa carries hazards between ESP32s; Python does not simulate that link.

Select **Get laptop location** and grant browser/OS location permission. Localhost is a secure context for this API. A laptop may estimate location using Wi-Fi rather than satellite GPS; correct/exact GPS is not guaranteed. The app shows coordinates and the reported accuracy circle. Poor accuracy is displayed but reports above 100 m, and confirmations above 25 m, are rejected. Reports require a fresh fix. No IP-based fallback or fabricated measured location is used.

Some laptop location providers need internet to obtain a fix. If needed, capture a location while connected to internet, then join the ESP32 hotspot and send within the 120-second fix window. The app does not guarantee satellite GPS precision.

The map library, offline coordinate grid, hazard markers and accuracy circle work without internet. **Street tiles** optionally uses OpenStreetMap and needs internet; attribution remains visible. Select **Fit locations** to view imported hazards. Clicking the map or selecting shared coordinates creates explicitly labelled manual TEST coordinates, not a measured location.

For a clean demonstration, erase BOTH boards over USB before resuming either. Firmware command `DEMO_RESET <own_node_id>` clears records, observations and permanent retirement markers from BOTH snapshot slots, while retaining node identity and its monotonic ID counter. It puts LoRa in a persistent paused state and prevents new reports. HTTP cannot request erasure. After both resets succeed, select **Resume LoRa after both nodes are reset** on any app that still shows a paused node (or issue `DEMO_RESUME <own_node_id>`). An already-running node has this button disabled. Old peers outside the reset group could still send old data; keep them off for this fresh demo.

Create a fresh report on A, wait for its exact ID on B, then select it and confirm from B using a fresh matching location within 25 m. With two fresh independent supporters the prototype score is 70. If browser location is unavailable/too inaccurate, shared test coordinates allow an explicitly simulated coordinate demonstration.

Queries and polling do not set UTC. New actions set UTC explicitly; retry preserves key, target, coordinates and timestamps. The laptop app refuses writes when its expected node identity is not found.
