#!/usr/bin/env bash
# Download the pinned prebuilt ONNX Runtime for Linux into Camera/third_party/ (SCRUM-176).
#
#   Camera/scripts/fetch_onnxruntime.sh            # aarch64 (FRDM-IMX95), the default
#   Camera/scripts/fetch_onnxruntime.sh x64        # x86_64 Linux host (tests on a PC)
#
# The archive is checked against a pinned SHA-256 before it is unpacked. CMakeLists.txt picks
# up Camera/third_party/onnxruntime-linux-<arch>-<version> automatically on Linux.
# macOS keeps using Homebrew's onnxruntime (see README).
set -euo pipefail

VERSION="1.19.0"
ARCH="${1:-aarch64}"
case "$ARCH" in
    aarch64) SHA256="5e1c92c63086564997e1a99f4e4ca7fad023cc34995d092f90d9cf5131629a00" ;;
    x64)     SHA256="8627cae49b51544ed15bb70c8ecf25b187f22a301c99c1ced5f4f3af5c36b44c" ;;
    *) echo "usage: $0 [aarch64|x64]" >&2; exit 2 ;;
esac

CAMERA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$CAMERA_DIR/third_party"
NAME="onnxruntime-linux-$ARCH-$VERSION"
URL="https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$NAME.tgz"

if [ -f "$DEST/$NAME/lib/libonnxruntime.so" ]; then
    echo "Already present: $DEST/$NAME"
    exit 0
fi

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

mkdir -p "$DEST"
TMP="$(mktemp "$DEST/.download.XXXXXX")"
trap 'rm -f "$TMP"' EXIT

echo "Downloading $URL"
curl --fail --location --silent --show-error --output "$TMP" "$URL"

ACTUAL="$(sha256_of "$TMP")"
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "ERROR: checksum mismatch for $NAME.tgz" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $ACTUAL" >&2
    exit 1
fi

tar -xzf "$TMP" -C "$DEST"
echo "OK: $DEST/$NAME (sha256 verified)"
