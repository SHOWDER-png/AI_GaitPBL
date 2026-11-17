# Porting the camera app to the FRDM-i.MX95 (SCRUM-176)

The same sources build for macOS (development) and Linux aarch64 (the board). **Only CMake
settings differ; no logic code changes are needed.** This was checked by compiling every C++
file in `Camera/src` and `Camera/tests` for aarch64 unchanged, and by running the logic tests
as aarch64 binaries (evidence below).

## Files

| File | Purpose |
|---|---|
| `cmake/aarch64.cmake` | Cross toolchain: NXP Yocto SDK (`CC`/`CXX`/`OECORE_TARGET_SYSROOT`) or `aarch64-linux-gnu-g++` + `FRDM_SYSROOT`; Cortex-A55 flags; uses qemu for `ctest` when installed |
| `CMakeLists.txt` | Platform detection (macOS / Linux, native or cross); finds ONNX Runtime in `third_party/`; RPATH `$ORIGIN/lib`; links ONNX Runtime into every test that includes it (the Linux link failure is fixed) |
| `scripts/fetch_onnxruntime.sh` | Downloads the pinned ONNX Runtime 1.19.0 (`aarch64` or `x64`) and checks its SHA-256 |
| `scripts/deploy.sh` | Copies app + model + `libonnxruntime.so.1` + `run_posture.sh` to the board over scp; refuses non-aarch64 binaries; `--dry-run` |

## Steps

```
# 1. ONNX Runtime for the board (once)
Camera/scripts/fetch_onnxruntime.sh                    # -> Camera/third_party/onnxruntime-linux-aarch64-1.19.0

# 2. Cross build (needs a sysroot with OpenCV dev files, see "Open items")
source /opt/fsl-imx-*/environment-setup-armv8a-poky-linux   # NXP SDK, or: export FRDM_SYSROOT=...
cmake -S Camera -B Camera/build-imx95 -DCMAKE_TOOLCHAIN_FILE=Camera/cmake/aarch64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build Camera/build-imx95 -j

# 3. Deploy and start
Camera/scripts/deploy.sh root@<board-ip> --build Camera/build-imx95      # add --dry-run to preview
ssh root@<board-ip> /root/gait/run_posture.sh
```

On a Linux PC (x86_64) the tests run natively: `Camera/scripts/fetch_onnxruntime.sh x64`, then
`cmake -S Camera -B build && cmake --build build && (cd build && ctest)`.

## Verified (17 Nov 2026, cloud Linux host, no board)

| Check | Result |
|---|---|
| Linux x86_64 build with the new CMake (OpenCV 4.6, ONNX Runtime 1.19.0) | all 8 targets build; `ctest` **6/6 pass**, including `frame_quality_replay` and `occlusion_replay` with the real `yolov8n-pose.onnx` on the fixtures |
| Before the CMake fix | `offline_validation_tests` and `frame_quality_tests` failed to link on Linux (`undefined reference to OrtGetApiBase`) |
| aarch64 compile of all 11 C++ files, unchanged, `aarch64-linux-gnu-g++ 13.3 -mcpu=cortex-a55` | 11/11 compile |
| aarch64 binaries (`ELF 64-bit ... ARM aarch64`) linked against ONNX Runtime 1.19.0 aarch64, run in `qemu-aarch64` | `frame_quality_tests`, `occlusion_fallback_tests`, `board_camera_tests`: all pass |
| Toolchain file through CMake without an OpenCV sysroot | picks `aarch64-linux-gnu-g++`, reports cross build, stops at `find_package(OpenCV)` (expected) |
| `deploy.sh` | dry run; refuses an x86 binary, a missing build, a relative or unsafe `--dest`, no board; full run through ssh/scp stand-ins copies the 4 files, and the aarch64 binary starts from the deployed folder finding `lib/libonnxruntime.so.1` via `$ORIGIN/lib` (and fails without it: negative control) |
| `shellcheck` on both scripts | clean |

Evidence: `results/scrum176-port-prep-20261117/verification.txt`.

## Open items (need the board or the NXP SDK)

1. **OpenCV for the target is the blocker for a full cross link.** The board image has OpenCV
   4.12 for Python only, no C++ headers or `opencv4.pc` (SCRUM-191). Either add
   `opencv-dev` + `gstreamer1.0-dev` to the Yocto image and install the matching SDK
   (`bitbake <image> -c populate_sdk`), or build natively on the board after installing those
   packages. `posture_detection` and `camera_capture` could not be linked for aarch64 here.
2. **Not run on the board yet**: `deploy.sh` against the real board, `run_posture.sh`, and
   inference speed on the Cortex-A55. Report on-board numbers separately from the macOS baseline.
3. **ONNX Runtime version**: the board build is pinned to 1.19.0 (as before); the Mac results used
   1.28.0 (`Integration/INTERFACE_CONTRACTS.md`, "Version mismatch to resolve"). Both run the same
   model; pick one before the NPU / quantization work (SCRUM-193/194) and update the pin.
4. The README's model download link (`ultralytics/assets` v8.2.0 `yolov8n-pose.onnx`) returns
   404. The model is already committed at `Camera/models/yolov8n-pose.onnx` (13,447,319 bytes).
