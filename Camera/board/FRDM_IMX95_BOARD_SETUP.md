# FRDM-i.MX95 board setup and camera bring-up

Covers SCRUM-189 (Linux BSP baseline) and SCRUM-191 (MIPI-CSI camera integration).
Only report what was actually observed on the board. Do not report unsupported paths as working.

## Hardware connections

| Board connector | Connect to |
|---|---|
| J25 USB-C PWR | USB-C PD power adapter |
| J1 USB-C Debug | Mac (serial console; shows on the Mac as "USB Quad_Serial", wch.cn) |
| J11 MIPI-CSI (or J10 DSI/CSI) | EXPI-OS08A20 via 22-pin FPC, contacts facing the right way, latches closed |
| J17 HDMI | Monitor |
| RJ45 (optional) | Same network as the Mac, for copying files with scp |

Write down which FPC connector (J10 or J11) the camera uses. The device tree must match it.

## 1. Open the serial console (Mac)

```
ls /dev/cu.usbmodem*                 # four ports appear when the debug cable is in
screen /dev/cu.usbmodem01234567891 115200   # the first port is the Linux console (ttyLP0)
```
In zsh, don't put a glob that may match nothing inside `$(...)`; zsh aborts the whole command.
Paste only short lines into the serial console: long lines get characters swapped.
If `screen` exits immediately, an old session still holds the port: run `screen -r` (reattach) or `pkill screen`.
Press the board's reset button to see the boot log. Exit screen with `Ctrl-A` then `K`.
If no new port appears, install the WCH serial driver for macOS (the chip is WCH, VID 0x1a86 PID 0x55d5).

Log in as `root` (no password). The `login:` prompt may only appear after pressing Enter.

## Verified results (5 Nov 2026, SCRUM-189)

| Item | Result |
|---|---|
| Board | `NXP FRDM-IMX95` |
| Image | NXP i.MX Release Distro 6.18-whinlatter, `imx95-15x15-lpddr4x-frdm` |
| Kernel | `6.18.2-1.0.0-gf49f45233f7b` |
| libcamera | `v0.0.0+6489-lf-6.18.2-1.0.0`, pipeline handler `nxp/neo` (needs `export LIBCAMERA_PIPELINES_MATCH_LIST=nxp/neo`) |
| Camera | OS08A20 on **J11 MIPI CSI1**, `cam -l` lists `External camera 'os08a20'` (i2c@42540000, addr 0x36) |
| Capture | `cam -c 1 --capture=10`: 29.5–29.9 fps, 3840x2160 default |
| Output sizes | **1920x1080 and 3840x2160 only** (all formats). 1280x720 is rejected with `not-negotiated`. Capture 1080p and scale. |
| HW converters | `imxvideoconvert_g2d`, `imxvideoconvert_ocl`, `imxcompositor_g2d` |
| Dev tools | `g++`, `cmake`, `python3` present. OpenCV: **not found via pkg-config** (`opencv4.pc` missing); check `find_package(OpenCV)` / `/usr/lib/cmake/opencv4` when building in SCRUM-191. |

Known issues:
- **Power**: needs a USB-PD adapter of at least 65 W on J25. Weaker sources make the board power-cycle every few seconds.
- **HDMI / Weston**: with the 7" HDMI screen, `card0-HDMI-A-1` reads 0-byte EDID and no modes, so `weston.service` fails (`no available modes for HDMI-A-1`) and nothing is displayed. Not yet tested with another monitor. Until Weston runs, `waylandsink` cannot show the camera.
- **Clock**: board time is wrong (shows Aug 05). Set it before recording timestamped data.

## 2. Run the BSP check (SCRUM-189)

Copy `board/frdm_bsp_check.sh` to the board. Either:
- Over the network: `scp Camera/board/frdm_bsp_check.sh root@<board-ip>:/root/` (find the IP with `ip addr` on the board), or
- With a USB stick, or by pasting the file into `cat > frdm_bsp_check.sh` in the serial console.

On the board:
```
cd /root && sh frdm_bsp_check.sh
cat bsp-check-*.log
```
The script only reads. It records the image, kernel, the device-tree and camera kernel messages, `/dev/video*` and media graph, `cam -l`, a 10-frame libcamera capture, a 150-frame GStreamer test, and which dev tools are installed.

What a working baseline looks like:
- Section 2 shows the OS08A20 sensor being probed (no errors).
- `cam -l` lists one camera.
- `cam -c 1 --capture=10` exits 0 and reports 10 frames.
- The GStreamer test exits 0.

If any of these fail, the blocker is recorded in the log. That still completes SCRUM-189's "verified or any blocker clearly recorded" requirement.

Attach the log to SCRUM-189.

## 3. Build and run the camera app on the board (SCRUM-191)

Needs: `g++`, `cmake` (present), OpenCV with GStreamer support (to be confirmed), `libcamerasrc` (present).
Note: the camera cannot output 1280x720 directly, so the pipeline (`src/board_camera.h`) captures 1920x1080 YUY2 and scales to 1280x720 with the G2D hardware converter. See section 4 for the measured results and the Python check that runs on the board today (the board has no C++ OpenCV headers yet).

```
export LIBCAMERA_PIPELINES_MATCH_LIST=nxp/neo
cd AI_GaitPBL/Camera
cmake -S . -B build-imx95 && cmake --build build-imx95 --target camera_capture -j4

# a. Initialization and one frame, then clean release
./build-imx95/camera_capture --board --check

# b. Frame delivery: 8 s per phase, PNG frames + frames.csv with timestamps and observed fps
./build-imx95/camera_capture --board --output captures/SCRUM-191-$(date +%Y%m%d-%H%M%S)
```
`--board` uses `libcamerasrc ! NV12 ! videoconvert ! BGR ! appsink`, the same pipeline as `main.cpp` on Linux.
Use `--width/--height/--fps` to try other modes, or `--gst "<pipeline>"` for a custom pipeline.

Record for SCRUM-191:
- The `--check` output (negotiated resolution, fps, "Camera released cleanly").
- Observed fps per phase from the capture summary. This is the on-board number; keep it separate from the macOS baseline (10 fps, 77.8 ms inference).
- One sample frame from the capture folder.
- Any limitation (format, resolution not negotiated, dropped frames, stability).


## 4. SCRUM-191 camera integration: measured on the board (11 Nov 2026)

Everything below was run on the FRDM-i.MX95 over the serial console. Evidence:
`results/scrum191-board-check-20261111/` (`report.json`, `sample_frame.jpg`).

### Pipeline findings

| Pipeline (all `libcamerasrc`, NeoISP) | Result on the board |
|---|---|
| No `format=` in caps | libcamera picks the **raw `R8/RAW` stream**, not ISP output. About 2.9 fps through OpenCV. Do not use. |
| `framerate=30/1` inside the `libcamerasrc` caps | `not-negotiated`. Set the rate after the converter with `videorate`. |
| `format=YUY2`, 1080p, CPU `videoconvert ! videoscale` to BGR 720p | 2.5 fps with frame loss. CPU colour conversion is the bottleneck. |
| `format=YUY2`, 1080p, `imxvideoconvert_g2d` to BGRx 720p | **60 fps**, no frame loss (gst-launch, 8 s) |
| `format=YUY2`, 2160p, `imxvideoconvert_g2d` to BGRx 720p | 29.8 fps, no frame loss |

Chosen pipeline (default in `board_camera::build_pipeline`):
```
libcamerasrc ! video/x-raw,format=YUY2,width=1920,height=1080 ! imxvideoconvert_g2d !
video/x-raw,format=BGRx,width=1280,height=720 ! videorate ! video/x-raw,framerate=30/1 !
videoconvert ! video/x-raw,format=BGR ! appsink drop=true max-buffers=1 sync=false
```

### Repeatable check (run on the board)

Copy `board/board_camera_check.py` to the board (USB stick, or base64 over the serial console), then:
```
python3 board_camera_check.py --capture 1920x1080 --frames 300 --cycles 3 --outdir board-camera-check
cat board-camera-check/report.json
```
Exit code 0 = PASS, 1 = a check failed, 2 = camera not usable. `--capture 3840x2160` tests the 4K path, `--cpu` the slow CPU path.

Result (1080p capture, 300 frames, OpenCV 4.12.0 with GStreamer 1.26.6 on the board):
- 300/300 frames received, shape 720x1280x3 uint8, not black
- **30.19 fps measured on the board**, longest gap 37.1 ms, 0 gaps over 100 ms
- 3/3 clean start/stop cycles
- 4K capture: 30.72 fps, longest gap 36.7 ms (150 frames)
- This is an on-board number. It is not comparable to the macOS baseline (10 fps with inference).

### Limitations (open)

1. **Green colour cast.** `sample_frame.jpg` shows a real, sharp scene but with a strong green cast. The CPU path shows the same cast, so it is not the G2D converter. 4K is greener than 1080p. Likely the ISP white balance or colour tuning (NXP ships tuning for 3840x2160 only). The camera was pointed at a ceiling. Retest aimed at a normal scene and check pose detection on real frames before relying on it.
2. **C++ application not yet built on the board.** The image has no OpenCV or GStreamer development files (`opencv4.pc` and `gstreamer-app-1.0.pc` are missing), so `camera_capture` and `posture_detection` cannot be compiled there. The Python check uses the identical pipeline through the board's OpenCV. Next step: install the OpenCV/GStreamer dev packages in the image, or cross-compile with the NXP SDK.
3. **Board clock** is wrong (Aug 2025), so timestamps in the report are not real time.
4. **Weston / HDMI.** The earlier "0-byte EDID" failure did not occur when the 7" screen was already connected: `systemctl restart weston` started it, and `card0-HDMI-A-1` reported `connected`. At boot Weston failed once before the display was ready. A restart fixes it; a boot-order fix is still open.
