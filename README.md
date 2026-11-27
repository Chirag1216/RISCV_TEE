# rvtee — RISC-V TEE Safety Monitor for Autonomous Drones

`rvtee` is a bare-metal RISC-V Trusted Execution Environment (TEE) that isolates flight-safety verification and actuator control from a potentially compromised normal-world OS (Linux 6.12 `PREEMPT_RT` or an adversarial S-mode payload) using Physical Memory Protection (PMP) and hardware privilege separation.

## Architecture

- **M-Mode Security Monitor (`monitor/`)**: Runs at `0x8000_0000`, enforces two PMP views (`PMP_VIEW_NORMAL` vs. `PMP_VIEW_TRUSTED`), handles SBI calls (`SBI_EID_DTEE`), copies requests to a private mailbox, and enforces hardware timers.
- **Trusted U-Mode Verifier (`verifier/`)**: Embedded binary loaded into PMP-protected `TCODE`/`TDATA` (`0xFFE0_0000`–`0xFFF0_0000`). Runs integer-only, FPU/VPU-off, zero-libc, zero-`.data` checks and is the **only** software allowed to write to the mock actuator (`ACT_BASE` at `0xFFF0_0000`).
- **Untrusted Normal World (`linux/`, `payload/`)**: Runs in S/U-mode at `0x8020_0000`, communicating exclusively through SBI `DTEE_FID_REGISTER`, `DTEE_FID_SUBMIT`, and `DTEE_FID_MISSION`.

## Security Guarantees

- **Key Confidentiality**: `K_GCS` (32-byte HMAC-SHA256 key) lives in `.rodata` inside PMP-locked `TCODE`, unreadable and unwritable from the normal world.
- **Mission & Command Integrity**:
  - **Signed GCS Corridors (`DTEE_FID_MISSION`)**: Up to 16 waypoints + room bounds + `v_max_mm_s` + corridor radius `R` authenticated with constant-time HMAC-SHA256 (`V_REJECT_AUTH` on mismatch, `V_REJECT_SEQ` on replay).
  - **6 Physical Checks (`DTEE_FID_SUBMIT`)**: Sequence monotonicity, 50 Hz rate limit, mode-transition state machine, room geofence + integer dot-product corridor check, teleport cap, and speed limit.
- **Failsafe & Liveness**:
  - **5 ms Verifier Watchdog**: Preempts hung or faulting verifiers (`V_TIMEOUT` / `V_FAULT`), forces `MODE_LAND`, and triggers post-failsafe state lockout (`MODE_LAND -> MODE_FLIGHT` blocked until clean `DISARMED -> ARMED -> FLIGHT` recovery).
  - **500 ms M-Mode Dead-Man's Switch**: Hardware `MTIMECMP` timer armed on accepted `MODE_FLIGHT` commands; forces `MODE_LAND` if the normal world stops sending valid flight commands.

## Quickstart

```bash
# Default build (with test hooks & SBI_EID_DEBUG) and S-mode test suite
make clean && make
make run

# Release build (strips VERIFIER_TEST_HOOKS and SBI_EID_DEBUG)
make clean && make RELEASE=1

# Boot Linux 6.12 PREEMPT_RT on top of the monitor
make linux

# Replay a 20 Hz command trace through Linux /dev/dtee
make replay CMD=data/placeholder_clean.cmd

# Calibrate policy.h from clean trajectories & run EuRoC evaluations
python3 tools/calibrate.py data/MH_01.txt data/MH_02.txt data/MH_03.txt --write
tools/run_experiments.sh data/MH_04.txt 400
tools/sweep_teleport.sh data/MH_04.txt
python3 tools/eval_corridor_drift.py data/MH_04.txt
```

## Verified Results

### PROTC-Style 4 Micro-Benchmarks (S-mode, QEMU `virt`)

| Benchmark | Workload | Outcome | Latency (`min / avg / max` cycles) | `0.1 us` ticks (`min / avg / max`) |
| --- | --- | --- | --- | --- |
| `DI-ACCESS` | 50 direct volatile writes (no TEE) | `50/50` writes | `767 / 3347 / 121639` | `3 / 13 / 509` |
| `AUTH-FULL (pose)` | 50 `DTEE_FID_SUBMIT` poses (25 ms paced) | `50/50` accepted | `223824 / 391838 / 700006` | `957 / 1686 / 3018` |
| `AUTH-FULL (hmac)` | 10 `DTEE_FID_MISSION` HMAC checks | `10/10` timed | `228028 / 343393 / 695440` | `992 / 1485 / 2947` |
| `AUTH-PART` | 10 valid + 10 policy violations | `10/10` accepted, `10/10` blocked | — | — |
| `UNAUTH` | 8 direct R/W attacks (`MON`/`TCODE`/`TDATA`/`ACT`) | `8/8` trapped by PMP | — | — |

### EuRoC MAV Unseen Evaluation (`MH_04`, `MH_05`; calibrated on `MH_01`–`MH_03`, `V_MAX=4180 mm/s`, `d_min=259 mm`)

| Attack | `MH_04` FPR / TPR | `MH_04` Latency | `MH_05` FPR / TPR | `MH_05` Latency |
| --- | --- | --- | --- | --- |
| `clean` | `0.00%` / — | — | `0.00%` / — | — |
| `teleport` (`2.0 m`) | `0.00%` / `100.00%` | `0.00 s` (`JUMP`) | `0.00%` / `100.00%` | `0.00 s` (`JUMP`) |
| `overspeed` (`2x V_MAX`) | `0.00%` / `3.00%` | `0.00 s` (`SPEED`) | `0.00%` / `100.00%` | `0.00 s` (`SPEED`) |
| `fence` | `0.00%` / `100.00%` | `0.00 s` (`FENCE`) | `0.00%` / `100.00%` | `0.00 s` (`FENCE`) |
| `replay` | `0.00%` / `100.00%` | `0.00 s` (`SEQ`) | `0.00%` / `100.00%` | `0.00 s` (`SEQ`) |
| `flood` | `0.00%` / `100.00%` | `0.05 s` (`RATE`) | `0.00%` / `100.00%` | `0.05 s` (`RATE`) |
| `mode` | `0.00%` / `100.00%` | `0.00 s` (`MODE`) | `0.00%` / `100.00%` | `0.00 s` (`MODE`) |

### Lateral `+Y` Slow-Drift: Static Room Box (Phase 1) vs. 20 s-Leg Signed Corridor (`R = 500 mm`, Phase 2)

| Dataset | Drift (`m/s`) | Box Clean FPR | Box TPR / Max Err | Corridor Clean FPR | Corridor TPR / Max Err |
| --- | --- | --- | --- | --- | --- |
| `MH_04` | `0.1` | `40.49%` | `53.34%` / `0.61 m` | `0.00%` | `83.91%` / `0.68 m` |
| `MH_04` | `0.2` | `40.49%` | `59.11%` / `1.22 m` | `0.00%` | `89.68%` / `0.66 m` |
| `MH_04` | `0.3` | `40.49%` | `87.65%` / `1.83 m` | `0.00%` | `94.53%` / `0.81 m` |
| `MH_04` | `0.5` | `40.49%` | `87.65%` / `3.05 m` | `0.00%` | `96.76%` / `0.80 m` |
| `MH_05` | `0.1` | `41.45%` | `58.78%` / `0.16 m` | `0.00%` | `88.21%` / `0.53 m` |
| `MH_05` | `0.2` | `41.45%` | `72.55%` / `0.35 m` | `0.00%` | `94.60%` / `0.60 m` |
| `MH_05` | `0.3` | `41.45%` | `91.00%` / `0.60 m` | `0.00%` | `95.95%` / `0.68 m` |
| `MH_05` | `0.5` | `41.45%` | `88.93%` / `1.57 m` | `0.00%` | `96.76%` / `0.90 m` |

### TCB Footprint (`make RELEASE=1`)

| Component | Source LOC | `.text` (bytes) | `.data` (bytes) | `.bss` (bytes) | Total ELF (bytes) |
| --- | --- | --- | --- | --- | --- |
| `monitor/` | `1,263` | `9,744` | `0` | `20,624` | `30,368` |
| `verifier/` (`verifier.bin`: `3,392` B) | `508` | `3,392` | `0` | `20,776` | `24,168` |
| `include/` | `278` | — | — | — | — |
| **Total TCB** | **`2,049`** | — | — | — | — |
