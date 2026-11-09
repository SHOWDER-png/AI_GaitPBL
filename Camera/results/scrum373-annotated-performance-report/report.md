# SCRUM-373 — Annotated output and local performance report

Generated from checked-in SCRUM-371/372 evidence at `2026-10-07T13:13:40Z`.
This is a five-fixture macOS development-host run, not a continuous
benchmark and not an i.MX95 performance result.

## Measured results

| Measure | Observed | Reference / judgment |
|---|---:|---|
| Fixture image resolution | 1280×720 px | 1280×720 requested and negotiated |
| Camera capture rate | standing 20.62, walking 22.61, no_person 23.03 FPS saved | 30 FPS requested; backend reports 30 FPS |
| Valid detection frames | 4/5 (80.0%) | No-person is intentionally included in the denominator |
| Positive-pose fixture detections | 4/5 overall; 4/4 labelled-person samples | 24/24 lower-body points valid at the configured confidence cutoff |
| Mean inference latency | 78.78 ms | Target < 100 ms: PASS |
| P95 inference latency | 88.51 ms | Measured; no P95 target documented |
| Inference throughput | 12.69 FPS | Target > 15 FPS: FAIL |
| Failed inferences | 0 | PASS (recorded run status) |

The valid-detection rate is `accepted person detections / all fixture frames`; the no-person frame is expected to be rejected, so this rate is not a standalone model-quality score. The positive fixture detection count and lower-body point-valid count are reported separately.

Inference latency is measured around `PoseDetector::detect` only; image decoding and output writing are excluded. FPS is successful inference calls divided by summed inference latency. The p95 uses nearest-rank selection over 5 calls, including the no-person call, and is statistically weak at this sample size. These are measured values, not targets.

### Performance verdict

- Mean latency target (<100 ms): PASS (78.78 ms measured).
- Throughput target (>15 FPS): FAIL (12.69 FPS measured).
- P95: measured only; the available baseline states no P95 threshold.
- No deployment or i.MX95 target is evaluated by these macOS results.
- The mean-latency and FPS thresholds are the existing README baseline comparison values, not Jira acceptance criteria.

## Model, runtime, and source run

- Model: YOLOv8n-pose ONNX; the README download URL names asset release v8.2.0, which is not independently verified as the model's training version.
- SHA-256: `a84b96c1bdaff04cb4cc77b052f337989c605836ff59388c22faabc21b96ce52`; the model file in this worktree matches the recorded SHA-256.
- ONNX Runtime: `1.28.0`.
- OpenCV: `5.0.0`.
- Execution platform: macOS development host, as identified by the recorded FaceTime HD camera configuration; exact CPU model and macOS build were not recorded.
- Capture source run: `Camera/captures/SCRUM-370-20261006-123844` (531 metadata rows; 509 images were available when the SCRUM-370 capture evidence was written).
- Full ignored capture directory present in this worktree: no. Curated source PNGs and their fixture-manifest hashes are verified.
- Repository-root `build/posture_detection` executable present: no. Saved SCRUM-371 results were used without rerunning inference; build the target before reproducing that inference command.

## Camera setup and observed limitations

- Camera: FaceTime HD Camera (reported by macOS) (Built-in integrated camera), device 0; front-facing.
- Placement: 60 cm high, 150 cm from subject.
- Lighting: Two room lights on; daytime; curtains closed.
- Exposure: Unavailable from OpenCV backend (CAP_PROP_EXPOSURE=-1).
- The selected walking images retain a real, furnished room background; the reviewed lower body and feet are in frame. The standing fixture is a single still sample.
- The lowest lower-body confidence in the detected fixtures is 0.836; it remains above the configured 0.50 validity cutoff. No selected lower-body point is invalid.
- The available fixture set contains no specifically labelled occlusion challenge and no ground-truth joint annotations. Occlusion robustness and quantitative pose accuracy therefore remain untested; no occlusion failure is inferred.
- The no-person frame correctly has no accepted detection and six missing lower-body records. This is the expected negative case, not a failed inference.
- Recorded inference failures: 0. The full raw SCRUM-370 capture directory is absent in this worktree, so its 509 original PNGs cannot be reviewed here. The SCRUM-370 metadata reports 22 missing standing images (frames 1–22); this is a capture-data gap, not an inference failure.
- The checked-in fixture images crop the subject above the waist in the reviewed views; do not use these frames to assess whole-body visibility or independently confirm anatomical left/right labels.

## Selected examples and annotations

Every annotation is generated from the unchanged PNG and SCRUM-372 coordinates/statuses. Green denotes `valid`; amber denotes `invalid`; gray denotes `missing`. The right-hand panel gives each point's model index, raw confidence, and validity state. No keypoint is synthesized; no-person frames show the absence of a detection.

| Role | Fixture | Detection / low point | Annotated image |
|---|---|---|---|
| representative standing | [`standing/standing_frame_94.png`](../../fixtures/standing/standing_frame_94.png) | accepted, person conf. 0.912; lowest lower-body point right_hip 0.977 (valid) | [standing__standing_frame_94.svg](annotations/standing__standing_frame_94.svg) |
| additional far-walking context | [`walking/walking_far_frame_251.png`](../../fixtures/walking/walking_far_frame_251.png) | accepted, person conf. 0.877; lowest lower-body point right_ankle 0.991 (valid) | [walking__walking_far_frame_251.svg](annotations/walking__walking_far_frame_251.svg) |
| representative walking (mid-distance fixture) | [`walking/walking_mid_frame_298.png`](../../fixtures/walking/walking_mid_frame_298.png) | accepted, person conf. 0.887; lowest lower-body point right_hip 0.995 (valid) | [walking__walking_mid_frame_298.svg](annotations/walking__walking_mid_frame_298.svg) |
| lowest-confidence detected walking sample | [`walking/walking_near_frame_346.png`](../../fixtures/walking/walking_near_frame_346.png) | accepted, person conf. 0.899; lowest lower-body point left_hip 0.836 (valid) | [walking__walking_near_frame_346.svg](annotations/walking__walking_near_frame_346.svg) |
| expected no-person negative | [`no_person/no_person_frame_439.png`](../../fixtures/no_person/no_person_frame_439.png) | no accepted detection | [no_person__no_person_frame_439.svg](annotations/no_person__no_person_frame_439.svg) |

## Reproduction commands

Run from the repository root. The first command documents the SCRUM-370 capture procedure; it recaptures into a new local directory and does not recreate the unavailable original run.

```sh
./build/camera_capture --device 0 --width 1280 --height 720 --fps 30 --seconds 8 --output "Camera/captures/SCRUM-370-$(date +%Y%m%d-%H%M%S)"
```

For the checked-in SCRUM-371 fixture inference:

```sh
cd Camera && ../build/posture_detection --offline --manifest fixtures/manifest.csv --threshold 0.3 --keypoint-threshold 0.5 --output results/scrum371-run-20261006/offline-inference
```

Regenerate the SCRUM-372 lower-body records into a new dated output directory (preserve the existing evidence file):

```sh
python3 Camera/tools/lower_body_records.py --input Camera/results/scrum371-run-20261006/offline-inference.jsonl --manifest Camera/fixtures/manifest.csv --output-dir Camera/results/scrum372-run-20261007-recheck
```

Regenerate the SCRUM-373 report and SVG annotations from the checked-in SCRUM-371 and SCRUM-372 records:

```sh
python3 Camera/tools/generate_scrum373_report.py --output-dir Camera/results/scrum373-annotated-performance-report
```

## Output schema and follow-up

- `report.md`: measured results, honest target verdicts, evidence limitations, selected fixtures, and reproduction commands.
- `annotations/*.svg`: reviewable vector overlays; each references its unchanged PNG under `Camera/fixtures/` using a relative path. Each includes six lower-body point labels and confidence/validity.
- `outputs.json`: schema `scrum373-results.v1`; records generation time, source artifacts, verified metrics, and each selected fixture/annotation plus point states.
- Existing SCRUM-371 JSONL/summary/run config and SCRUM-372 JSONL remain read-only inputs; original fixture PNGs are not edited.

Follow-up evidence links:
- Robustness capture/fixture guidance: [fixture inventory](../../fixtures/README.md) and [capture procedure](../../CAMERA_CAPTURE.md). Expand the set with consented occlusion, lighting, distances, and repeated walking sequences before making robustness claims.
- Jira robustness follow-up: [SCRUM-201 — Camera Occlusion Fallback](https://kmitl-team-vbds25lw.atlassian.net/browse/SCRUM-201). The issue title is relevant, but it currently has no description; confirm its scope before treating it as an occlusion-test plan.
- i.MX95 port/performance follow-up: [Camera CMake platform configuration](../../CMakeLists.txt). Build and measure this same fixture inference on FRDM-i.MX95 and record its native model/runtime/board versions separately; the macOS measurements above do not predict target performance.
- Jira porting follow-up: [SCRUM-176 — Port Preparation for FRDM-IMX95](https://kmitl-team-vbds25lw.atlassian.net/browse/SCRUM-176), whose scope covers the aarch64 CMake toolchain, ONNX Runtime, and deployment to the board. It does not establish that pose inference has been ported or benchmarked.
