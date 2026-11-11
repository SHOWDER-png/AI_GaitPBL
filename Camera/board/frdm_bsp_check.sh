#!/bin/sh
# SCRUM-189: FRDM-i.MX95 BSP and camera-stack baseline check.
# Run ON THE BOARD as root:  sh frdm_bsp_check.sh
# Writes everything to bsp-check-<timestamp>.log in the current directory.
# Read-only: it changes no configuration.

LOG="bsp-check-$(date -u +%Y%m%dT%H%M%SZ).log"
exec > "$LOG" 2>&1
export LIBCAMERA_PIPELINES_MATCH_LIST=nxp/neo

section() { echo; echo "===== $1 ====="; }
run() { echo "\$ $*"; "$@" 2>&1; echo "[exit $?]"; }
have() { command -v "$1" >/dev/null 2>&1; }

section "1. Board, image and kernel"
run cat /proc/device-tree/model; echo
run cat /etc/os-release
run uname -a
run cat /proc/cmdline
run ls /boot /run/media/boot-mmcblk0p1 2>/dev/null
run free -m
run df -h /

section "2. Camera kernel messages (OS08A20 / CSI / ISP)"
dmesg | grep -i -E "os08a20|csi|isi|neoisp|ov08|camera|mipi" | tail -60
echo "[matched lines above; empty means the sensor was not probed]"

section "3. Device nodes"
run ls -l /dev/video* /dev/media* /dev/v4l-subdev*
have v4l2-ctl && run v4l2-ctl --list-devices
have media-ctl && for m in /dev/media*; do run media-ctl -d "$m" -p; done

section "4. libcamera"
if have cam; then
  run cam --version
  run cam -l
  echo "--- capture 10 frames from camera 1 (no files written)"
  run timeout 30 cam -c 1 --capture=10
else
  echo "cam (libcamera) NOT installed"
fi

section "5. GStreamer"
if have gst-launch-1.0; then
  run gst-launch-1.0 --version
  run gst-inspect-1.0 libcamerasrc
  echo "--- 150 NV12 1280x720 frames through libcamerasrc into fakesink"
  START=$(date +%s.%N)
  run timeout 40 gst-launch-1.0 -q libcamerasrc num-buffers=150 ! \
      video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 ! fakesink sync=false
  END=$(date +%s.%N)
  echo "wall time for 150 frames: $(echo "$END - $START" | bc 2>/dev/null) s (includes startup)"
else
  echo "gst-launch-1.0 NOT installed"
fi

section "6. Development / runtime tools"
for t in gcc g++ cmake make pkg-config python3 git; do
  if have $t; then echo "$t: $($t --version 2>&1 | head -1)"; else echo "$t: MISSING"; fi
done
pkg-config --modversion opencv4 2>/dev/null | sed 's/^/opencv4: /' || echo "opencv4: MISSING (pkg-config)"
python3 -c "import cv2; print('python cv2', cv2.__version__); print([l for l in cv2.getBuildInformation().splitlines() if 'GStreamer' in l])" 2>&1
ls -d /root/onnxruntime-linux-aarch64-* /usr/lib/libonnxruntime* /usr/lib/aarch64-linux-gnu/libonnxruntime* 2>/dev/null || echo "onnxruntime: not found in expected paths"
ls /usr/lib/libethosu* /usr/lib/libvx_delegate* /usr/lib/libneutron* 2>/dev/null || echo "NPU delegate libs: none found in /usr/lib"

section "Done"
echo "Log: $LOG"
