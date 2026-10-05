# Local Camera Capture

This capture utility is independent of pose inference, the IMU, BLE, and the i.MX95 pipeline. Raw captures are saved locally under `Camera/captures/` and are ignored by Git.

## Build and Check

Open the `Camera` folder in VS Code so CMake Tools uses its `CMakeLists.txt` as the project root. Run **CMake: Select a Kit**, then **CMake: Configure**, choose `camera_capture` as the build target, and run **CMake: Build**. The executable is produced in the configured build directory (the existing local setup uses `Camera/build/`).

First ask the utility to open the camera and read one frame without saving:

```sh
./Camera/build/camera_capture --check --device 0 --width 1280 --height 720 --fps 30
```

It prints the requested settings, backend-reported settings, first-frame dimensions, and the backend exposure property where available. A mismatch is printed as a limitation; the stage summary later reports observed FPS. Open failure and first-frame failure are non-zero errors with actionable messages.

## Capture Command

Use a new output path for each run. Existing output paths are never overwritten.

```sh
./Camera/build/camera_capture \
  --device 0 --width 1280 --height 720 --fps 30 --seconds 8 \
  --output "Camera/captures/SCRUM-370-$(date +%Y%m%d-%H%M%S)"
```

Press Enter at each prompt. Capture standing still, then normal walking with the complete lower body and feet visible, then leave the scene empty for the no-person stage. The utility writes lossless PNG frames into phase folders and a `frames.csv` containing a global frame index, UTC timestamp, monotonic elapsed time, dimensions, read duration, estimated drops, and image path. It stops with an error if reading or saving a frame fails and preserves any partial run for diagnosis.

The estimated dropped-frame value is inferred from gaps between successful reads and the requested FPS. It is not a hardware/backend dropped-frame counter; file writing and other capture-loop delays can contribute to it. The per-phase observed FPS is saved-frame throughput and includes file-writing time. Check that `frame_index` and `elapsed_monotonic_ms` increase throughout `frames.csv`, and record the printed resolution/FPS limitations and drop totals in `capture-config.json`.

## Record the Physical Setup

Before calling the run reproducible, fill `Camera/capture-config.json` with the observed camera make/model, USB or built-in connection, device index, negotiated resolution and observed FPS, exposure mode/value if available, lighting conditions, measured mounting height, camera-to-subject distance, and viewing direction. Confirm from the saved walking images that hips, knees, ankles, and feet stay in frame for the full sequence. The 90-100 cm height, 2-3 m distance, and front view in the config are prior application guidance only, not measurements of this machine's setup.

## Fixtures and Privacy

The capture command preserves the complete original run locally. Curate only a small set of useful examples into `Camera/fixtures/` after capture: at least a few full-body walking frames and a no-person frame, plus standing frames if useful. Keep each source frame unchanged; copy it into the fixture folder rather than editing the original. Do not commit a person's image without consent. Prefer a consenting, non-identifiable test subject; inspect each selected image for faces, screens, and other personal information before staging it. Raw runs remain ignored and should not be attached to Jira unless reviewed and explicitly approved.

`Camera/fixtures/README.md` tracks what is still missing. The fixture set is not complete until real local frames have been collected and reviewed.

## Troubleshooting

- **Permission denied or camera unavailable on macOS:** In System Settings, open Privacy & Security > Camera and allow the terminal application that launches the program (Terminal, iTerm, or VS Code). Quit/reopen that application after changing permission. On Linux, check the user's access to the camera device and the active camera service.
- **Cannot open device index:** Check the cable/connection, close other apps that may own the camera, and retry `--check` with another `--device` index. The index is backend-dependent; the default is 0.
- **Camera opens but no first frame/read fails:** Reconnect and power-cycle the camera, close competing capture applications, then retry. The error identifies whether open or frame delivery failed.
- **Resolution or FPS limitation:** Use the actual dimensions and observed stage FPS printed by the program in the config. Try a supported mode if the requested 1280x720 at 30 FPS is unavailable. `CAP_PROP_FPS` may be unsupported or inaccurate on some backends; rely on the timed stage observation as well.
- **Dim, flickering, or blurred image:** Improve even front lighting, avoid backlighting, and reduce subject motion blur with brighter light. The utility does not force exposure or gain; record the camera's auto/manual exposure setting and the lighting used.
- **Hips, ankles, or feet leave the image:** Keep the camera level and fixed, move it farther away or lower/raise the mount as needed, and verify the entire walking path at the start, middle, and end before recording. Record the final measured height, distance, and direction in the config.