#!/bin/bash
# Packages the no-UI network harness (`network_peer`) as a standalone WUHB.
#
# The harness needs no game assets: it only opens sockets and prints results.
# Its configuration is read from the SD card
# (`sd:/wiiu/apps/devilutionx/network_peer.ini`), and its JSONL result is
# written next to it, so nothing has to be inside the WUHB.
#
# Usage: tools/wiiu/build-network-peer.sh [output.wuhb]
#
# Environment overrides:
#   WIIU_BUILD_DIR  harness build directory (default: <repo>/build-wiiu-net)
#   WIIU_ICON       128x128 icon png       (default: <repo>/Packaging/wiiu/icon.png)
#   DEVKITPRO       devkitPro root         (default: /opt/devkitpro)
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-net}
ICON=${WIIU_ICON:-$REPO/Packaging/wiiu/icon.png}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
ELF2RPL="$DEVKITPRO/tools/bin/elf2rpl"
WUHBT="$DEVKITPRO/tools/bin/wuhbtool"

OUT=${1:-$BUILD/network_peer.wuhb}

if [ ! -f "$BUILD/network_peer.elf" ]; then
  echo "error: $BUILD/network_peer.elf not found -- build with:" >&2
  echo "       cmake -S . -B build-wiiu-net -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake -DBUILD_NETWORK_HARNESS=ON" >&2
  echo "       make -C build-wiiu-net network_peer -j8" >&2
  exit 1
fi

# Cemu 2.6.0 takes `tweq` traps even when the condition is false; see
# cemu-tweq-workaround.py. Must run before elf2rpl.
python3 "$REPO/tools/wiiu/cemu-tweq-workaround.py" "$BUILD/network_peer.elf" | tail -3

"$ELF2RPL" "$BUILD/network_peer.elf" "$BUILD/network_peer.rpx" >/dev/null 2>&1 \
  || { echo "error: elf2rpl failed (is the ELF unstripped?)" >&2; exit 1; }

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/wiiu-peer-wuhb.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets"

ARGS=( "$BUILD/network_peer.rpx" "$OUT" --content="$STAGE" \
       --name "DevilutionX Network Peer" --author "Devilution Team" )
if [ -f "$ICON" ]; then
  ARGS+=( --icon="$ICON" )
fi

"$WUHBT" "${ARGS[@]}" >/dev/null 2>&1 || { echo "error: wuhbtool failed" >&2; exit 1; }

echo "rpl:  $(stat -f%z "$BUILD/network_peer.rpx" 2>/dev/null || stat -c%s "$BUILD/network_peer.rpx") bytes"
echo "wuhb: $OUT ($(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") bytes)"
