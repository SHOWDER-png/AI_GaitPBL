# Frame quality handling: no person, stale data, low confidence (SCRUM-380)

Decides for every frame whether a pose detection may be used for gait metrics. Code:
`src/frame_quality.h` (header only, no camera or model needed). Used by `src/main.cpp`.

## Rules

Checked in this order; the first that applies wins.

| Status | When | What the app does |
|---|---|---|
| `INVALID_DATA` | Timestamp is NaN/infinite, in the future, or goes backwards; detection has fewer than 17 keypoints or a non-finite person confidence | Ignore the frame, show "Invalid frame data" |
| `STALE_DATA` | Result is older than 500 ms when it is used (`now - capture`) | Ignore, show "Stale data: result too old" |
| `NO_PERSON` | The detector returned no person | Ignore, show "No person detected" |
| `LOW_CONFIDENCE` | Person confidence below 0.3, or a required lower-body keypoint is non-finite or below 0.5 | Ignore, show "Low confidence" and the first reason |
| `OK` | None of the above | Compute and log gait metrics |

Required keypoints: both knees and both ankles always. Hips are required by default; `main.cpp`
sets `hips_required = false` because `compute_gait` has a virtual-hip fallback. Upper-body
keypoints never matter. Thresholds are the existing ones (person 0.3, keypoint 0.5).

On any status other than `OK` the app resets the current gait result, so the test keys
`A`/`B`/`E`/`D` can never evaluate an old result. Status changes are printed as
`>> [quality] OK -> NO_PERSON | no person detected`, and a per-status frame count is printed at quit.

Timing: `capture_ms` is taken right after `cap.read()`, `now_ms` after inference, both on the
monotonic clock. Normal age is the inference time (about 80 ms on the Mac).

## How to verify (all run on the Mac, no camera needed)

```
cd Camera
cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build -j4
cd build && ctest --output-on-failure           # unit tests + replay
cd .. && ./build/frame_quality_replay           # replay on the real fixtures
```

Expected replay output (also saved in `results/scrum380-frame-quality-20261111/replay.txt`):

```
standing/standing_frame_94.png,OK,6,STALE_DATA,-
walking/walking_far_frame_251.png,OK,6,STALE_DATA,-
walking/walking_mid_frame_298.png,OK,6,STALE_DATA,-
walking/walking_near_frame_346.png,OK,6,STALE_DATA,-
no_person/no_person_frame_439.png,NO_PERSON,0,STALE_DATA,no person detected
REPLAY PASS (5 fixtures)
```
The `late_verdict` column re-checks the same detection as if it were 800 ms old.

The unit tests (`tests/frame_quality_tests.cpp`) cover every status, the exact boundaries
(0.3, 0.5, 500 ms), NaN/infinite values, backwards timestamps, malformed detections, the
priority order, hips optional vs required, and recovery after bad frames. Five deliberate bugs
(`>` to `>=` on age, `<` to `<=` on person confidence, `>=` to `>` on keypoint confidence,
removing the backwards-timestamp check, ignoring `hips_required`) were each caught.

## Manual check still needed (live camera)

Run `./build/posture_detection`, finish calibration, then:
1. Step out of view: the screen shows "No person detected" and the console prints `OK -> NO_PERSON`.
2. Cover your legs or stand very close so the ankles leave the frame: "Low confidence".
3. Step back in: status returns to `OK`.
4. Press `Q` and read the "Frame quality summary" line.

## Limitations

- `STALE_DATA` only triggers when inference plus capture takes over 500 ms; a camera that stops
  delivering frames makes `cap.read()` fail and the loop exits as before.
- Quality is checked after calibration; during calibration frames are already filtered by the
  existing 0.5 keypoint check.
- The 500 ms limit and the keypoint rule are untuned on the board (inference there is not yet measured).
