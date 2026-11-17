# Camera occlusion fallback (SCRUM-201)

`src/occlusion_fallback.h` keeps the camera output usable when one leg is hidden for a moment
(in a front view one leg often passes behind the other while walking). Before this, the
frame-quality gate (SCRUM-380) rejected the whole frame as LOW_CONFIDENCE and every metric
went blank.

## States

| Per leg | When | Values shown |
|---|---|---|
| `VISIBLE` | hip (real or virtual) + knee + ankle reliable (score >= 0.5), knee angle measured | live |
| `HELD` | not visible now, last visible <= 300 ms ago | last live knee angle and compression, phase `HELD` |
| `OCCLUDED` | not visible for > 300 ms, or never seen | none (`N/A`) |

| Overall | When | Effect |
|---|---|---|
| `FULL` | both legs VISIBLE | normal display, symmetry shown |
| `PARTIAL` | one or both legs HELD, or one leg OCCLUDED with the other VISIBLE/HELD | metrics of the measurable leg; symmetry hidden |
| `CAMERA_DEGRADED` | both legs OCCLUDED | "Camera degraded: use IMU gait phase"; `use_imu_fallback = true` |

## Rules

- A held value is never older than `max_hold_ms` (default 300 ms, about 3 frames at the 10 FPS
  M1 baseline). `max_hold_ms = 0` turns holding off.
- Symmetry needs both legs VISIBLE in the same frame. A held leg never produces symmetry.
- Time comes from the caller's monotonic clock. A non-finite time or time going backwards drops
  every hold and reports CAMERA_DEGRADED for that frame.
- **Display only.** `gait_data.csv` (I4, training data) is unchanged: it still logs only frames
  that pass the quality gate, with `-1` for anything not measured. Held values are never
  written to the CSV, and the A/B/E/D validation keys still read only live, fully gated values.

## How it is wired in `main.cpp`

1. Quality gate OK: as before (CSV log), then the tracker sees the frame (normally FULL).
2. Quality gate LOW_CONFIDENCE, but a person is detected with enough confidence: `compute_gait`
   runs on what is visible, the tracker holds or drops the hidden leg, the overlay shows
   `L leg HELD 120 ms: left knee hidden` or `OCCLUDED`. Nothing is logged.
3. No person, stale or invalid frame: the tracker is updated with no detection, so holds expire.
4. Mode changes are printed (`>> [occlusion] FULL -> PARTIAL | L HELD R VISIBLE`) and a
   per-mode count is printed at quit.

## Tests (no camera, model or person)

```
cmake -S Camera -B build && cmake --build build --target occlusion_fallback_tests
./build/occlusion_fallback_tests        # or: cd build && ctest -R occlusion
```

10 test groups use the real keypoints the pose model found on the recorded fixtures
(`results/scrum372-run-20261007`: standing_frame_94, walking_mid_frame_298) and hide joints
to simulate occlusion: hold, expiry at exactly 300 ms, recovery, both legs lost, never seen,
clock going backwards / NaN, hold disabled, display view, virtual hip. Seven deliberately
injected bugs were each caught. Evidence: `results/scrum201-occlusion-20261117/`.

## Limitations / not done

- Not yet run on live video or with the real model on occluded frames: the model file
  cannot be fetched in the build environment. A real-occlusion check (recorded walking
  frames with one leg passing behind the other) is still open.
- `use_imu_fallback` is a flag only. Switching the gait phase to the IMU (Kaan's IMU-only gait
  phase, SCRUM-179 chain) needs that component and the common time base (SCRUM-192).
- 300 ms is a starting value, not tuned on footage.
