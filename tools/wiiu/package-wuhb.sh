#!/bin/bash
# Packages a self-contained Aroma-layout WUHB for the Wii U port.
#
# Inside the WUHB (mounted read-only at /vol/content by Aroma *and* Cemu):
#
#   /content/assets/...      game data: spawn.mpq, fonts/, data/, *.gmo, ...
#
# Writable data (diablo.ini, save_*.sv, mods/, screenshots) goes to
# /vol/external01/wiiu/apps/devilutionx/ on the SD card, which
# Source/platform/wiiu/init.cpp creates on first run. Nothing is written next
# to the WUHB or into the SD card root.
#
# Usage: tools/wiiu/package-wuhb.sh [output.wuhb] [--no-rpl]
#
# Environment overrides:
#   WIIU_BUILD_DIR   build directory      (default: <repo>/build-wiiu-release)
#   WIIU_ASSETS_DIR  built assets dir     (default: <repo>/build-wiiu-clean/assets)
#   WIIU_ICON        128x128 icon png     (default: <repo>/Packaging/wiiu/icon.png)
#   DEVKITPRO        devkitPro root       (default: /opt/devkitpro)
#
# Note: the ELF must already be tweq-patched (see cemu-tweq-workaround.py) —
# wiiu-run.sh does that before calling this script.
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-release}
ASSETS=${WIIU_ASSETS_DIR:-$REPO/build-wiiu-clean/assets}
ICON=${WIIU_ICON:-$REPO/Packaging/wiiu/icon.png}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
ELF2RPL="$DEVKITPRO/tools/bin/elf2rpl"
WUHBT="$DEVKITPRO/tools/bin/wuhbtool"

OUT=$BUILD/devilutionx.wuhb
DO_RPL=1
for arg in "$@"; do
  case "$arg" in
    --no-rpl) DO_RPL=0 ;;
    *) OUT=$arg ;;
  esac
done

if [ ! -d "$ASSETS" ]; then
  echo "error: assets directory not found: $ASSETS" >&2
  echo "       (build with BUILD_ASSETS_MPQ=ON, or set WIIU_ASSETS_DIR)" >&2
  exit 1
fi
if [ ! -f "$ASSETS/spawn.mpq" ]; then
  echo "error: $ASSETS/spawn.mpq not found — the WUHB would be unbootable" >&2
  exit 1
fi

if [ "$DO_RPL" = 1 ]; then
  if [ ! -f "$BUILD/devilutionx.elf" ]; then
    echo "error: $BUILD/devilutionx.elf not found — build first" >&2
    exit 1
  fi
  "$ELF2RPL" "$BUILD/devilutionx.elf" "$BUILD/devilutionx.rpx" >/dev/null 2>&1 \
    || { echo "error: elf2rpl failed" >&2; exit 1; }
  echo "rpl:     $(stat -f%z "$BUILD/devilutionx.rpx" 2>/dev/null || stat -c%s "$BUILD/devilutionx.rpx") bytes"
fi

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/wiiu-wuhb.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets"
cp -R "$ASSETS/." "$STAGE/assets/"

ARGS=( "$BUILD/devilutionx.rpx" "$OUT" --content="$STAGE" \
       --name "DevilutionX" --author "Devilution Team" )
if [ -f "$ICON" ]; then
  ARGS+=( --icon="$ICON" )
fi

"$WUHBT" "${ARGS[@]}" >/dev/null 2>&1 || { echo "error: wuhbtool failed" >&2; exit 1; }

echo "content: $STAGE/assets ($(du -sh "$ASSETS" | cut -f1))"
echo "wuhb:    $OUT ($(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") bytes)"
