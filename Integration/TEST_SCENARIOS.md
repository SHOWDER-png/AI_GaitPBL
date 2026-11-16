# Functional, error and performance scenarios (SCRUM-352)

Scenario catalog for the full-system integration test (SCRUM-180). Status as of 11 Nov 2026.
Order of execution (from SCRUM-180): component tests, then pairwise interface tests, then the
complete walking session. The end-to-end test is **not claimed complete** until every interface
gap in `INTERFACE_CONTRACTS.md` (G1 to G6) is closed.

Status words: **PASS** = run and passed (command and date given), **READY** = automated or fully
scripted, not yet run by the team, **MANUAL** = needs a person, a camera or hardware, **BLOCKED** =
cannot run until the named gap is closed. Requirement IDs and targets come from
`Specification_AI_Assist_Gait.docx` (team spec, sections 6 and 2-3); see "Requirement traceability"
below. Only six requirements carry an `AG-SWR-` ID there; the other targets are cited as
"Spec §6" until the full SRS IDs are added.

## Smoke (does each piece start)

| ID | Scenario | How to run | Expected | Status |
|---|---|---|---|---|
| SM-01 | Contracts have not drifted | `python3 -B Integration/tools/check_contracts.py` | `NO DRIFT`, exit 0 | PASS 11 Nov (24 checks, IMU files now under `Nordic/firmware/experiments/imu_bmi270_c29/`) |
| SM-02 | Synthetic fixtures give the recorded verdicts | `python3 -B -m unittest discover -s Integration/tests -v` | 12 tests OK | PASS 11 Nov |
| SM-03 | Host C++ unit tests | `cd Camera && cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build -j4 && cd build && ctest` | 4 of 4 pass | PASS 11 Nov |
| SM-04 | Board camera starts, delivers frames, stops cleanly | on the board: `python3 board_camera_check.py --frames 300 --cycles 3` | `"result": "PASS"`, exit 0 | PASS 11 Nov (30.19 fps); re-run MANUAL after any image change |
| SM-05 | Pose model runs on the fixture frames | `Camera/build/frame_quality_replay` | `REPLAY PASS (5 fixtures)` | PASS 11 Nov |
| SM-06 | Thingy:53 streams IMU data over USB | flash firmware, open serial 115200 baud | CSV lines `t_ms,ax,...,gz` | MANUAL (Kaan) |
| SM-07 | FRDM-IMX95 boots to a login prompt on the debug console | power on, open the first serial port | `imx95-15x15-lpddr4x-frdm login:` | PASS 11 Nov |

## Functional (does it do the right thing)

| ID | Scenario | Steps / data | Expected | Status |
|---|---|---|---|---|
| FN-01 | Camera records become valid lower-body records | `Integration/tests` test `test_lower_body_producer_output_is_valid` | validator reports 0 errors; phases standing, walking, no_person | PASS 11 Nov |
| FN-02 | Standing posture | `posture_detection`, calibrate, press `A` | pass criteria as coded for Test A in `main.cpp` | MANUAL (live camera) |
| FN-03 | Semi-squat | same, press `B` | pass criteria as coded for Test B | MANUAL |
| FN-04 | Deep squat | same, press `E` | pass criteria as coded for Test C | MANUAL |
| FN-05 | One leg raised | same, press `D` | symmetry above 30 percent | MANUAL |
| FN-06 | IMU captures follow the documented axis convention | rotate/tilt the Thingy as in `imu_config.yaml` samples | signs match the convention | MANUAL (Kaan) |
| FN-07 | Camera and IMU timestamps line up | record both with one trigger event | offset ≤ 20 ms (AG-SWR-205) | BLOCKED by G6 / SCRUM-192 |
| FN-08 | Six FSR channels reach the node | press each sensor | one channel changes per press | BLOCKED by G1 |
| FN-09 | IMU and FSR data reach the FRDM-IMX95 over BLE | pair, stream, store on board | records accepted, counts match | BLOCKED by G1, G2, G3 |
| FN-10 | Analysis result and alert reach the GUI and audio | walk, trigger an asymmetry | alert shown and spoken | BLOCKED by G4, G5 |

## Error and recovery (does it fail safely)

| ID | Scenario | How | Expected | Status |
|---|---|---|---|---|
| ER-01 | No person in view | unit test + replay on `no_person` fixture | status `NO_PERSON`, no gait row written | PASS 11 Nov |
| ER-02 | Legs not clearly visible | `frame_quality_tests` (weak ankle, knee) | status `LOW_CONFIDENCE`, reason names the keypoint | PASS 11 Nov |
| ER-03 | Result older than 500 ms | `frame_quality_tests`, replay "late" column | status `STALE_DATA`, ignored | PASS 11 Nov |
| ER-04 | Bad timestamps (NaN, future, backwards) | `frame_quality_tests` | status `INVALID_DATA` | PASS 11 Nov |
| ER-05 | Malformed or truncated data is rejected with a reason | 33 synthetic fixtures | each gets the verdict in `expected.json` | PASS 11 Nov |
| ER-06 | IMU dropout longer than 100 ms is flagged | fixture `imu_gap.csv` | one warning, file still valid | PASS 11 Nov |
| ER-07 | Camera unplugged during a run | pull the CSI cable or stop the stream | app exits with a clear error, files closed | MANUAL (board) |
| ER-08 | Display not ready at boot | boot with the HDMI screen attached | Weston starts; if not, `systemctl restart weston` recovers | MANUAL (open issue, see board doc) |
| ER-09 | BLE link drops and returns | walk out of range | automatic reconnect without user action (AG-SWR-104), gap recorded, no duplicate records | BLOCKED by G2 |
| ER-10 | Corrupted packet from the node | inject a bad checksum | rejected with reason, counted | BLOCKED by G1, G3 |
| ER-11 | Camera occluded | cover the legs during a walk | below 60 % pose confidence the app switches to IMU + pressure only and shows a warning banner (AG-SWR-204) | BLOCKED by SCRUM-201 and G1-G3 (today only the `LOW_CONFIDENCE` status of ER-02 exists) |

## Performance

Targets are taken from the team spec (§6) and cited. Where the repository README had a different
number, the spec value is used and the README value is noted.

| ID | Scenario | Target | Measured so far | Status |
|---|---|---|---|---|
| PF-01 | Pose inference time, mean | below 100 ms (README) | 77.8 ms on macOS M1 (16 Aug); 78.8 ms on 5 fixtures; board not measured | macOS PASS, board MANUAL (SCRUM-194) |
| PF-02 | End-to-end frame rate (camera + pose + GUI) | ≥ 30 FPS GUI refresh (Spec §6); README used > 15 fps | 10.0 fps on macOS M1 (README baseline): **fails** both | FAIL on macOS; board not measured (needs NPU, SCRUM-194) |
| PF-03 | Camera delivery on the board | ≥ 30 fps (AG-SWR-203), no gap over 100 ms | 30.19 fps, longest gap 37.1 ms (300 frames, 11 Nov) | PASS |
| PF-04 | IMU sample rate | 50–100 Hz, configurable (AG-SWR-101); `imu_config.yaml` sets 100 Hz | 94 Hz observed in the C29 captures | PASS against AG-SWR-101; 6 % below the configured 100 Hz |
| PF-05 | Latency from deviation to spoken cue | ≤ 120 ms after the deviation is held > 200 ms (Spec §6) | not measured | BLOCKED by G4, G5 (TTS, SCRUM-196) |
| PF-06 | Soak and thermal | no target in the spec yet; SCRUM-248 | not measured | BLOCKED |
| PF-07 | Pressure sample rate | ≥ 50 Hz per channel, all six (AG-SWR-110) | not measured | BLOCKED by G1 |
| PF-08 | Stream synchronisation | ≤ 20 ms (AG-SWR-205) | not measured | BLOCKED by G6 / SCRUM-192 |
| PF-09 | Start-up and self-test | power-on to POST done ≤ 1.0 s, POST ≤ 0.16 s, config load ≤ 50 ms (Spec §6) | not measured | BLOCKED by SCRUM-219, SCRUM-222 |
| PF-10 | BLE connection time | 100–500 ms (Spec §6, with AG-SWR-104) | not measured | BLOCKED by G2 |

## Requirement traceability

| Requirement | Target | Scenarios | Status |
|---|---|---|---|
| AG-SWR-101 IMU sampling | 50–100 Hz, configurable | PF-04, FN-06, SM-06 | PASS (94 Hz) |
| AG-SWR-104 BLE reconnection | auto reconnect; connect in 100–500 ms | ER-09, PF-10 | BLOCKED (G2) |
| AG-SWR-110 Pressure sampling | ≥ 50 Hz per channel | PF-07, FN-08 | BLOCKED (G1) |
| AG-SWR-203 Camera frame rate | ≥ 30 fps | PF-03, SM-04 | PASS (30.19 fps) |
| AG-SWR-204 Occlusion fallback | < 60 % pose confidence → IMU + pressure only, warning | ER-11, ER-02 | BLOCKED (SCRUM-201) |
| AG-SWR-205 Stream sync | ≤ 20 ms | PF-08, FN-07 | BLOCKED (G6, SCRUM-192) |
| Spec §6 Pose inference | < 100 ms per frame | PF-01 | PASS on macOS, board not measured |
| Spec §6 GUI refresh | ≥ 30 FPS | PF-02 | FAIL on macOS (10 fps) |
| Spec §6 Audio feedback | ≤ 120 ms | PF-05 | BLOCKED |
| Spec §6 Start-up / POST | ≤ 1.0 s / ≤ 0.16 s / config ≤ 50 ms | PF-09 | BLOCKED |

Notes from the spec that affect testing:
- The spec lists OpenCV 5.0.0 and ONNX Runtime 1.28.0; the board image has OpenCV 4.12.0 and the Linux CMake path expects ONNX Runtime 1.19.0 (see the versions table in `INTERFACE_CONTRACTS.md`). Agree on one set before PF-01/PF-02 are measured on the board.
- The spec's display is a 7" HDMI touch LCD at 1024×600. With the current board image this screen gives no EDID and Weston fails (ER-08); the 1024×600 mode may need to be forced.
- The gait-phase accuracy target is still "to agree with mentor" in the spec, so no accuracy scenario is written yet.

## Recording a failure (SCRUM-180 step 5)

For every failed scenario record: scenario ID, UTC timestamp, the exact command or steps, the
log or output file, software and component versions (from `INTERFACE_CONTRACTS.md`), the owner,
and a one-line reproduction. Retest after the fix and note the date.

## Not covered yet

Everything marked BLOCKED, the requirements in the spec that still have no `AG-SWR-` ID, GUI usability
testing, and the signed results page that SCRUM-180 asks for at the end.
