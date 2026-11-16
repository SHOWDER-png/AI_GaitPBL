#!/usr/bin/env python3
"""FRDM-i.MX95 camera integration check (SCRUM-191). Run ON THE BOARD.

Opens the same GStreamer pipeline as src/board_camera.h (libcamera + NXP NeoISP, 1080p
captured and scaled to 1280x720 BGR) through OpenCV, and checks that the application gets
valid frames. It measures frame rate on the board, saves a sample frame, and tests clean
start/stop. Exit code 0 = all checks passed, 1 = a check failed, 2 = camera not usable.

    python3 board_camera_check.py [--frames 150] [--cycles 3] [--outdir DIR]
"""
import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time

os.environ.setdefault("LIBCAMERA_PIPELINES_MATCH_LIST", "nxp/neo")

import cv2  # noqa: E402  (after the environment variable so libcamera picks nxp/neo)

OUT_W, OUT_H, FPS = 1280, 720, 30
PIPELINE = ""


def build_pipeline(capture_w, capture_h, g2d=True):
    # Keep identical to board_camera::build_pipeline() in src/board_camera.h
    source = f"libcamerasrc ! video/x-raw,format=YUY2,width={capture_w},height={capture_h} ! "
    size = f",width={OUT_W},height={OUT_H}"
    rate = f"videorate ! video/x-raw,framerate={FPS}/1"
    sink = " ! appsink drop=true max-buffers=1 sync=false"
    if g2d:
        return (source + "imxvideoconvert_g2d ! video/x-raw,format=BGRx" + size + " ! " + rate +
                " ! videoconvert ! video/x-raw,format=BGR" + sink)
    return (source + "videoconvert ! videoscale ! video/x-raw,format=BGR" + size + " ! " + rate +
            sink)


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout.strip()
    except Exception as error:  # report, never crash the check
        return f"unavailable ({error})"


def open_camera():
    cap = cv2.VideoCapture(PIPELINE, cv2.CAP_GSTREAMER)
    return cap if cap.isOpened() else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames", type=int, default=150)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--outdir", default="board-camera-check")
    parser.add_argument("--capture", default="1920x1080",
                        help="libcamera capture size, 1920x1080 or 3840x2160")
    parser.add_argument("--cpu", action="store_true",
                        help="CPU convert/scale instead of the G2D hardware converter")
    args = parser.parse_args()
    global PIPELINE
    capture_w, capture_h = (int(v) for v in args.capture.lower().split("x"))
    PIPELINE = build_pipeline(capture_w, capture_h, g2d=not args.cpu)

    os.makedirs(args.outdir, exist_ok=True)
    report = {
        "board": open("/proc/device-tree/model").read().strip("\x00\n ") if os.path.exists(
            "/proc/device-tree/model") else platform.node(),
        "kernel": platform.release(),
        "opencv": cv2.__version__,
        "pipeline": PIPELINE,
        "capture_size": args.capture,
        "board_clock_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "checks": {},
        "limitations": [],
    }
    checks = report["checks"]

    checks["libcamerasrc_installed"] = shutil.which("gst-inspect-1.0") is not None and \
        "libcamerasrc" in run(["gst-inspect-1.0", "libcamerasrc"])
    checks["camera_listed"] = "os08a20" in run(["cam", "-l"]).lower()
    if not (checks["libcamerasrc_installed"] and checks["camera_listed"]):
        report["result"] = "NOT_USABLE"
        print(json.dumps(report, indent=2))
        return 2

    cap = open_camera()
    checks["pipeline_opened"] = cap is not None
    if cap is None:
        report["result"] = "NOT_USABLE"
        report["limitations"].append("OpenCV could not open the GStreamer pipeline.")
        print(json.dumps(report, indent=2))
        return 2

    stamps, shape_ok, dark_frames, first = [], True, 0, None
    for _ in range(args.frames):
        ok, frame = cap.read()
        if not ok or frame is None:
            checks["frame_read_failed_after"] = len(stamps)
            break
        stamps.append(time.monotonic())
        if first is None:
            first = frame.copy()
        if frame.shape != (OUT_H, OUT_W, 3) or frame.dtype.name != "uint8":
            shape_ok = False
        if frame.mean() < 2.0:
            dark_frames += 1
    cap.release()

    checks["frames_received"] = len(stamps)
    checks["frames_requested"] = args.frames
    checks["frame_shape_is_720x1280x3_uint8"] = shape_ok and len(stamps) > 0
    checks["frames_not_black"] = len(stamps) > 0 and dark_frames < len(stamps) * 0.1
    if len(stamps) > 1:
        span = stamps[-1] - stamps[0]
        gaps = [b - a for a, b in zip(stamps, stamps[1:])]
        report["measured_on_board"] = {
            "fps": round((len(stamps) - 1) / span, 2),
            "max_gap_ms": round(max(gaps) * 1000, 1),
            "frames_with_gap_over_100ms": sum(1 for g in gaps if g > 0.1),
        }
        if report["measured_on_board"]["fps"] < FPS * 0.9:
            report["limitations"].append(
                f"Measured {report['measured_on_board']['fps']} fps on the board, below the "
                f"requested {FPS} fps (includes OpenCV read and conversion time).")
    if first is not None:
        sample = os.path.join(args.outdir, "sample_frame.jpg")
        checks["sample_frame_saved"] = bool(cv2.imwrite(sample, first))
        report["sample_frame"] = sample

    cycles_ok = 0
    for _ in range(args.cycles):
        cap = open_camera()
        good = cap is not None and cap.read()[0]
        if cap is not None:
            cap.release()
        cycles_ok += 1 if good else 0
        time.sleep(0.5)
    checks["clean_start_stop_cycles"] = f"{cycles_ok}/{args.cycles}"

    passed = (checks["frames_received"] == args.frames
              and checks["frame_shape_is_720x1280x3_uint8"] and checks["frames_not_black"]
              and checks.get("sample_frame_saved", False) and cycles_ok == args.cycles)
    report["result"] = "PASS" if passed else "FAIL"
    with open(os.path.join(args.outdir, "report.json"), "w") as out:
        json.dump(report, out, indent=2)
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
