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

The command writes `offline-inference.jsonl` (one run-metadata record followed
by one record per input fixture) and `offline-inference-summary.json` in the
current directory. Change the `--output` prefix to change both names. Missing
images and inference exceptions are written as error records, reported to
stderr, included in `failed_frames`, and cause a non-zero exit code. Model or
manifest errors are reported explicitly.

`--threshold` is the minimum **person/detection confidence** used to accept a
detector candidate; it defaults to `0.3`, matching live inference. This does
not filter or change keypoint scores. `--keypoint-threshold` defaults to `0.5`
and is a separate reporting threshold: each of the 17 keypoints retains its
raw x/y coordinates and confidence, and its `valid` flag is true only when
coordinates and confidence are finite and the score meets this threshold.
Absent keypoints are emitted with null values and `valid: false`.

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
