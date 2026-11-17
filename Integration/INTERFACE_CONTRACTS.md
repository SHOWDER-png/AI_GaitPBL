# Interface contracts and component versions (SCRUM-348)

Status: **FROZEN.** Contract set `draft-2`. `draft-1` (I1-I5) was written 11 Nov 2026 and approved by the project lead (Natthanon) on 16 Nov 2026; `draft-2` (17 Nov 2026) adds I6 by its owner and leaves I1-I5 unchanged.
The contracts below are the ones that already exist in code or captured data. Nothing here was
invented: each row names the file that is the source of truth. Interfaces nobody has defined yet
are listed as gaps with their owner task; they are outside this freeze and each owner adds its
contract with its own task. Machine-readable copy: `contracts/frozen_contracts.json`.

## How the freeze is enforced

`python3 -B Integration/tools/check_contracts.py` compares the repository with the snapshot and
exits 1 on drift (a changed CSV header, schema version, IMU rate or range, a component version
recorded by a past run, or the pose model file size). It also validates every real artifact in the
repository. To change a contract: bump its version, update `frozen_contracts.json` and this file in
the same change, and get the consumer's owner to approve.

## Interfaces that exist today

| # | Interface | Producer to consumer | Format and version | Time base | Source of truth | Owner |
|---|---|---|---|---|---|---|
| I1 | Camera capture log | `camera_capture` to offline tools | CSV `frames.csv`, 9 columns, unversioned | `elapsed_monotonic_ms` (steady clock) + `utc_timestamp` | `Camera/src/camera_capture.cpp` | Natthanon |
| I2 | Fixture manifest | capture to offline inference | CSV `manifest.csv`, 10 columns incl. SHA-256 | as I1 | `Camera/fixtures/manifest.csv` | Natthanon |
| I3 | Lower-body record | camera pipeline to receiver / analysis | JSON Lines, `schema_version = lower-body-record.v1` | `source.elapsed_monotonic_ms`, `source.timestamp_utc` | `Camera/tools/lower_body_records.py` | Natthanon |
| I4 | Gait metrics log | `posture_detection` to analysis | CSV `gait_data.csv`, 12 columns, unversioned; `-1` = not visible | `ts_ms` since program start | `Camera/src/main.cpp` | Natthanon |
| I5 | IMU stream export | Thingy:53 (BMI270) to host | CSV, 7 columns; m/s^2 and rad/s; target 100 Hz; accel +/-2 g, gyro +/-500 dps | `t_ms` (see gap G6) | `Nordic/firmware/experiments/imu_bmi270_c29/imu_config.yaml` | Kaan |
| I6 | Camera ML training export | `export_ml_dataset.py` to Phase 3 training | CSV, 20 columns, `schema_version = camera-gait-ml.v1` in the `.meta.json` sidecar; empty cell = not measured | `t_ms` = I4 `ts_ms`, per session | `Camera/tools/export_ml_dataset.py`, `Camera/ML_EXPORT.md` | Natthanon |

Column lists are in `frozen_contracts.json`. Rules enforced by the reference validators
(`tools/contract_validators.py`): I3 has exactly six points in a fixed order, status in
`valid | invalid | missing`, a valid point has coordinates and confidence at or above the threshold
and no reasons, a no-person record has no valid points, timestamps never go backwards within a run.
I4: angles are `-1` or 0..180, compression 0..1, phase one of `STANDING | SEMI_SQUAT | DEEP_SQUAT`.
I5: finite numbers, whole `t_ms` that never goes backwards, values inside the configured ranges;
a gap longer than 100 ms is reported as a warning.

## Gaps: interfaces nobody has defined yet

Not part of this freeze. They block full-system integration (SCRUM-180), not SCRUM-348. They are
not drafted here on purpose, because inventing a packet layout for someone else's task would create
a false contract. When an owner defines one, it is added here as a new interface with a version bump.

| # | Missing contract | Owner task | Needed for |
|---|---|---|---|
| G1 | FSR pressure record (fields, units, channel count) and packet (version, counter, timestamp, count, checksum) | SCRUM-31 | pressure to node |
| G2 | BLE characteristic mapping and the Thingy:53 to FRDM-IMX95 transport | SCRUM-31 and the BLE task | IMU/FSR to board |
| G3 | Receiver accepted/rejected record store and rejection reasons | SCRUM-33 | board receive |
| G4 | Analysis result schema and alert rules | SCRUM-306 (Sprint 13) | analysis to GUI/audio |
| G5 | GUI / audio output messages | GUI Dashboard epic SCRUM-256 | output |
| G6 | Common time base between camera (monotonic ms) and Thingy (`t_ms`, "timestamps are PC-side" in the C29 notes) | SCRUM-192 | sensor sync |

## Component versions (observed, not chosen)

| Component | Version | Source |
|---|---|---|
| macOS dev machine | macOS 15.7.4, arm64 (MacBook Air M1) | `sw_vers` |
| ONNX Runtime (macOS) | 1.28.0 | `offline-inference-summary.json` |
| OpenCV (macOS) | 5.0.0 | `offline-inference-summary.json` |
| Pose model | `yolov8n-pose.onnx`, 13,447,319 bytes | `Camera/models/` |
| FRDM-IMX95 image | NXP i.MX Release Distro 6.18-whinlatter, kernel 6.18.2-1.0.0-gf49f45233f7b | `Camera/board/FRDM_IMX95_BOARD_SETUP.md` |
| libcamera / pipeline | v0.0.0+6489-lf-6.18.2-1.0.0, handler `nxp/neo` | board setup doc |
| OpenCV on the board | 4.12.0 (Python, GStreamer YES), no C++ headers | `report.json` of SCRUM-191 |
| GStreamer on the board | 1.26.6 | `report.json` of SCRUM-191 |
| Thingy:53 firmware | nRF Connect SDK v3.3.4 (Zephyr 4.3.99), BMI270 | `imu_config.yaml` |

**Version mismatch to resolve:** `Camera/CMakeLists.txt` points the Linux build at ONNX Runtime
1.19.0 (`/root/onnxruntime-linux-aarch64-1.19.0`) while the macOS results use 1.28.0, and OpenCV is
5.0.0 on the Mac against 4.12.0 on the board. Pick one ONNX Runtime and OpenCV version for the board
before the NPU/quantization work (SCRUM-193/194) and record it here.

## Approval

Approved by the project lead (Natthanon), 16 Nov 2026. A separate team sign-off is not required
for this freeze.
