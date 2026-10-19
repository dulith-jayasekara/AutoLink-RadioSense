# Component 4 Live Demonstration

This folder contains a small reproducible supervisor demonstration for the existing deterministic SCF simulation. It reuses the public `scf_sim::Simulation` API and compiles the real production SCF sources from `src/Store Carry Forward/`.

## Scenario

- A = node 1
- B = node 2
- C = node 3
- A-B contact: 0-100 ms with 10 ms latency
- B-C contact: 200-400 ms with 10 ms latency
- A and C never meet
- Tick interval: 10 ms
- Delivery means destination DATA acceptance

The runner executes the DTN-SCF scenario and the direct-only baseline using the same contact schedule. Direct-only delay is printed as `N/A` when no destination delivery occurs.

## Run

From PowerShell at the repository root:

```powershell
Set-Location D:\AutoLink-RadioSense
.\test\unit\scf\demo\run_demo.ps1
```

To specify another compiler path:

```powershell
.\test\unit\scf\demo\run_demo.ps1 -Compiler C:\msys64\ucrt64\bin\g++.exe
```

The script uses MSYS2 UCRT64 GCC with `-std=c++11 -Wall -Wextra -pedantic`. It creates temporary executable output outside the repository and writes these graph-ready files under the ignored demo output directory:

- `test/unit/scf/demo/output/dtn.csv`
- `test/unit/scf/demo/output/dtn_aggregate.csv`
- `test/unit/scf/demo/output/direct_only.csv`
- `test/unit/scf/demo/output/direct_only_aggregate.csv`

The two scenario CSV files contain per-message metrics. The two `_aggregate.csv` files contain aggregate metrics. The output directory is ignored by the demo's `.gitignore`.

## Expected reference checks

The runner verifies the scenario semantics, not hardcoded metric values: DTN must deliver the generated bundle through B, the forwarding hop count must be one, and direct-only must not deliver and must expire the bundle. The runner prints the actual delay, forwarding, retry, expiry, occupancy, and delivery metrics.

The results are deterministic simulation evidence only. They do not represent physical ESP32 behavior, RF propagation, live LoRa transmission, or multi-node hardware performance.
