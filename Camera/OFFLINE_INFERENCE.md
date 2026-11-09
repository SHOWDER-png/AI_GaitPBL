# Recorded-frame pose inference

Build the `posture_detection` target using the repository-root CMake Tools
configuration, then run from the `Camera/` directory:

```sh
../build/posture_detection --offline \
  --manifest fixtures/manifest.csv \
  --threshold 0.3 \
  --keypoint-threshold 0.5 \
  --output offline-inference
```

The manifest is read as CSV; fixture paths are resolved relative to its
directory. The checked-in SCRUM-370 manifest already contains standing,
walking, and no-person examples. The command does not access the camera.

The command writes `<output-prefix>.jsonl` (one run-metadata record followed
by one record per input fixture) and `<output-prefix>-summary.json`. Change
the `--output` prefix to change both names. Missing images and inference
exceptions are written as error records, reported to stderr, included in
`failed_frames`, and cause a non-zero exit code. Model or manifest errors are
reported explicitly.

## SCRUM-371 recorded run

Persistent output from the checked-in five-fixture set is saved under
`Camera/results/scrum371-run-20261006/`:

- `offline-inference.jsonl`: run metadata and five raw per-frame records.
- `offline-inference-summary.json`: aggregate frame and timing results.
- `run-config.json`: exact command, date, input/model identity, runtime
  versions, and confidence thresholds.

The command was run from the repository root as:

```sh
cd Camera && ../build/posture_detection --offline --manifest fixtures/manifest.csv --threshold 0.3 --keypoint-threshold 0.5 --output results/scrum371-run-20261006/offline-inference
```

For this saved run, all five fixtures were processed: **4 valid detections, 1
no-person frame, and 0 failures**. The no-person fixture
`no_person/no_person_frame_439.png` has `detection_valid: false`,
`person_confidence: null`, and an empty `keypoints` array. The walking fixtures
at frames 251, 298, and 346 all produced detections with person confidences
0.877, 0.887, and 0.899 respectively; their records preserve all 17
keypoints, with 10, 10, and 6 meeting the configured keypoint validity cutoff.

Measured for this run: **mean 78.78 ms**, **p95 88.51 ms**, and **12.69
inference-only FPS**, from five successful `PoseDetector::detect` calls
(including the no-person call). The p95 uses nearest-rank selection and is
statistically less reliable with only five samples. These measurements differ
from the earlier temporary run; this saved summary is calculated from this
run's per-frame latencies, not copied from the earlier run. Timing can vary
between runs with runtime scheduling and machine load.

`--threshold` is the minimum **person/detection confidence** used to accept a
detector candidate. The value `0.3` was selected to match both the existing
`PoseDetector::detect()` default and the threshold explicitly passed by the
live inference loop, so offline and live runs use the established setting.
The repository does not establish that `0.3` is scientifically optimal or
empirically tuned. This threshold does not filter or change keypoint scores.
`--keypoint-threshold` defaults to `0.5` and is a separate reporting
threshold: each of the 17 keypoints retains its raw x/y coordinates and
confidence, and its `valid` flag is true only when coordinates and confidence
are finite and the score meets this threshold. Absent keypoints are emitted
with null values and `valid: false`.

The model is identified by the repository as `yolov8n-pose.onnx` (YOLOv8n
pose); the README's download URL names the Ultralytics assets release `v8.2.0`.
The run config records the file's SHA-256 and size to identify the exact model
file used. ONNX Runtime and OpenCV versions are reported by the executable at
run time. The repository does not provide additional embedded model-version
metadata, so the asset URL release label should not be interpreted as a
separately verified model-training version.

Latency is measured with `steady_clock` around `PoseDetector::detect` only;
image decoding and result writing are excluded. Latency is in milliseconds.
Mean and nearest-rank p95 use successfully completed inference calls, including
calls that returned no person; the summary gives the sample count. FPS is the
successful inference-call count divided by summed inference latency in
seconds. P95 is statistically less reliable for this small fixture set.
Runtime/model versions, model path and size, thresholds, source fixture paths,
labels, frame identifiers, image dimensions, detection outcome, person score,
and keypoint values are included in the result files.

Focused checks can be run with CTest after configuring and building:

```sh
ctest --test-dir ../build --output-on-failure -R offline_validation_tests
```

From the repository root, run:

```sh
ctest --test-dir build --output-on-failure -R offline_validation_tests
```

SCRUM-372 derives timestamped left/right hip, knee, and ankle records from the
saved SCRUM-371 output without changing it. The model-index mapping, output
schema, point-validation reasons, duplicate-coordinate rule, example, and
focused test command are documented in
[LOWER_BODY_KEYPOINTS.md](LOWER_BODY_KEYPOINTS.md).

## SCRUM-373 annotated report

The checked-in report output is
`Camera/results/scrum373-annotated-performance-report/`:

- `report.md` contains measured fixture performance, explicit comparisons
  against the existing README latency/FPS targets, camera setup, evidence
  limitations, selected examples, and reproduction commands.
- `annotations/*.svg` are reviewable overlays on the unchanged PNG fixtures.
  The six lower-body model points are identified by index; the legend shows
  confidence and `valid`, `invalid`, or `missing` status. SVGs reference their
  source PNGs under `Camera/fixtures/` by relative path.
- `outputs.json` uses schema `scrum373-results.v1` and records source evidence,
  reconciled metrics, model/runtime identity, capture details, selected roles,
  and per-point state.

Regenerate the SCRUM-373 outputs from the checked-in SCRUM-371 inference and
SCRUM-372 lower-body records, from the repository root:

```sh
python3 Camera/tools/generate_scrum373_report.py \
  --output-dir Camera/results/scrum373-annotated-performance-report
```

The generator validates every fixture against its manifest SHA-256 and image
dimensions, pairs all SCRUM-371 frames with their SCRUM-372 records, recomputes
the timing/count metrics from per-frame results, and fails if they differ from
the saved SCRUM-371 summary. It also confirms the local model matches the
recorded hash. It does not run inference or modify fixture/input files.

The exact saved capture configuration, model/runtime versions, and SCRUM-371
command are in the preceding sections and
`Camera/results/scrum371-run-20261006/run-config.json`. Recapture guidance and
the SCRUM-370 capture command are in [CAMERA_CAPTURE.md](CAMERA_CAPTURE.md).
The full ignored SCRUM-370 capture directory is not included in this worktree;
only the curated fixture images and metadata are available. These measured
macOS fixture timings are not i.MX95 measurements. The platform-specific
build configuration is in [CMakeLists.txt](CMakeLists.txt); repeat inference
and record native runtime/board details on FRDM-i.MX95 before making target
performance claims.

Focused Python checks:

```sh
python3 -m unittest discover -s Camera/tests -p 'test_scrum373_report.py' -v
python3 -m unittest discover -s Camera/tests -p 'test_lower_body_records.py' -v
```
