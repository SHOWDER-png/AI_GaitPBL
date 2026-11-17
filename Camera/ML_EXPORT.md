# Camera gait data export for ML training (SCRUM-213)

`Camera/tools/export_ml_dataset.py` turns the `gait_data.csv` logs written by `posture_detection`
(interface I4) into one training CSV in schema **`camera-gait-ml.v1`**, plus a `.meta.json`
sidecar. Standard library only, no hardware needed.

```
# one or more recorded sessions -> one training file
python3 -B Camera/tools/export_ml_dataset.py path/to/session1/gait_data.csv path/to/session2/gait_data.csv \
    --output Camera/results/ml-export/camera_gait_ml.csv [--label normal]

# tests (15, all offline)
python3 -B -m unittest Camera/tests/test_export_ml_dataset.py -v
```

Exit code 0 = exported, 1 = rejected (nothing is written), 2 = usage error.

## What is validated

| Check | Rule | On failure |
|---|---|---|
| Column names | header must equal the frozen I4 header (12 columns) | reject |
| Data types | every numeric column parses as a finite number; `squat_phase` is `STANDING`, `SEMI_SQUAT` or `DEEP_SQUAT` | reject |
| Value ranges | angles `-1` or 0..180, compression 0..1, symmetry `-1` or >= 0, `infer_ms` >= 0 (reference validator `Integration/tools/contract_validators.py`) | reject |
| Time order | `ts_ms` never goes backwards; no repeated timestamp | reject |
| Sampling rate | rate = 1000 / median interval; must be >= `--min-rate-hz` (default 5 Hz) | reject |
| Gaps | an interval > `--gap-factor` x median (default 3x) is a gap (dropped frames) | warning, listed in the sidecar; `dt_ms` shows it per row |
| Size | at least `--min-samples` rows (default 10) | reject |
| Empty file | a 0-byte `gait_data.csv` (session not saved with Q) | reject with that hint |

The exporter never repairs data. Fix the capture and record again.

## Output schema `camera-gait-ml.v1`

One row per camera frame that passed the SCRUM-380 frame-quality gate.

| Column | Type | Meaning |
|---|---|---|
| `session_id` | str | `<parent folder>/<file stem>` of the source log |
| `sample_index` | int | 0-based row index within the session |
| `t_ms` | float | `ts_ms` from the source: ms since program start (per session) |
| `dt_ms` | float | interval to the previous row; empty on the first row |
| `l_knee_deg`, `r_knee_deg`, `l_hip_deg`, `r_hip_deg` | float | joint angles; **empty** when the source had `-1` (not visible) |
| `symmetry_pct` | float | symmetry index; empty when the source had `-1` |
| `l_compress`, `r_compress` | float | leg compression 0..1 |
| `l_knee_valid` ... `symmetry_valid` | 0/1 | 1 when the matching value is present |
| `squat_phase` | category | as logged |
| `squat_phase_id` | int | STANDING=0, SEMI_SQUAT=1, DEEP_SQUAT=2 |
| `infer_ms` | float | pose inference time for that frame |
| `label` | str | `--label` value; empty = unlabeled (labeling is a later task) |

Missing values are empty cells, never `-1`, so a model cannot learn the sentinel as an angle.
The sidecar records the schema, the rules used, each source file's SHA-256, and per-session
sampling statistics (rate, jitter, gaps).

## Limitations

- `t_ms` restarts at 0 in each session and is not aligned with IMU or pressure time
  (gap G6, SCRUM-192). Do not join sessions across sensors on `t_ms`.
- `l_hip_deg` / `r_hip_deg` are mostly empty: `compute_gait` only fills them when the shoulder is
  visible, and the front-view camera placement often cuts it off.
- The repository holds no recorded `gait_data.csv` yet: `Camera/gait_data.csv` is 0 bytes and is
  rejected. The exporter was verified on the synthetic I4 fixtures in
  `Integration/fixtures/synthetic/`. A real recorded session is needed before Phase 3 training.

Evidence: `Camera/results/scrum213-ml-export-20261117/` (`run.txt`, sample export and sidecar).
