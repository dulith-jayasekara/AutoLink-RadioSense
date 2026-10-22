# ESP32 hardware test projects

Each folder is a separate PlatformIO project. Run commands from the repository root:

```powershell
pio run -d test/embedded/scf_beacon_forwarder
```

| Folder | Purpose |
| --- | --- |
| `lora_receiver` | LoRa receiver diagnostic |
| `beacon_compat_receiver` | Existing Beacon decoder and receiver |
| `lora_scf_buffer_test` | LoRa and SCF buffer diagnostic |
| `scf` | SCF embedded test firmware |
| `scf_lifecycle_demo` | SCF lifecycle demonstration |
| `scf_native_data` | Native SCF DATA diagnostic |
| `scf_beacon_forwarder` | Node B: receive, store and manually forward |
| `scf_forwarded_receiver` | Node C: receive forwarded SCF DATA |

Read each project's README for its behavior, limits and hardware procedure.
Application code belongs in `src/`; local headers in `include/`; historical files
belong in `archive/` outside the compiled sources. Shared production SCF code stays
in the repository's `src/Store Carry Forward/` directory.
