#!/usr/bin/env bash
# Build gait_node in WSL (Linux filesystem = fast). See WSL_BUILD.md.
# Usage:  bash wsl_build.sh [pristine]
set -euo pipefail

NCS_VERSION="v3.3.4"
NCS_DIR="$HOME/ncs"
BOARD="thingy53/nrf5340/cpuapp"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK="$HOME/work/gait_node"
export PATH="$HOME/.local/bin:$PATH"

PRISTINE=""
[ "${1:-}" = "pristine" ] && PRISTINE="-p always"

mkdir -p "$WORK"
echo "== sync $SRC -> $WORK"
rsync -a --delete --exclude 'build*/' "$SRC/" "$WORK/"

echo "== build ($NCS_VERSION, $BOARD) ${PRISTINE:+[pristine]}"
cd "$WORK"
export ZEPHYR_BASE="$NCS_DIR/$NCS_VERSION/zephyr"  # lets west find "build" outside the SDK workspace
nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VERSION" --install-dir "$NCS_DIR" -- \
    west -z "$ZEPHYR_BASE" build -b "$BOARD" --sysbuild -d build $PRISTINE

OUT="$SRC/build/wsl"
mkdir -p "$OUT"
cp -f build/dfu_application.zip "$OUT/"
ls -l "$OUT/dfu_application.zip"
echo "BUILD_OK -> Windows path: gait_node\\build\\wsl\\dfu_application.zip"
