#!/bin/bash
# Packages the no-UI local-session runner (`local_session_runner`) as a
# self-contained WUHB, so it can boot on Cemu or a real console and run a
# scenario without any menu interaction.
#
# Inside the WUHB (mounted read-only at /vol/content by Aroma *and* Cemu):
#
#   /content/assets/...          game data: spawn.mpq, fonts/, data/, *.gmo, ...
#
# The runner must be self-contained: on the console it cannot read the assets of
# the main devilutionx.wuhb. Writable data goes to
# sd:/wiiu/apps/devilutionx/runner/ (created on first run), so it never touches
# the player's own saves.
#
# Usage: tools/wiiu/package-local-session.sh [output.wuhb]
#
# Environment overrides:
#   WIIU_BUILD_DIR   build directory   (default: <repo>/build-wiiu-net)
#   WIIU_ASSETS_DIR  assets directory  (default: <repo>/build-wiiu-clean/assets)
#   WIIU_ICON        128x128 icon png  (default: <repo>/Packaging/wiiu/icon.png)
#   DEVKITPRO        devkitPro root    (default: /opt/devkitpro)
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-net}
ASSETS=${WIIU_ASSETS_DIR:-$REPO/build-wiiu-clean/assets}
ICON=${WIIU_ICON:-$REPO/Packaging/wiiu/icon.png}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
ELF2RPL="$DEVKITPRO/tools/bin/elf2rpl"
WUHBT="$DEVKITPRO/tools/bin/wuhbtool"

OUT=${1:-$BUILD/local_session_runner.wuhb}

ELF="$BUILD/local_session_runner.elf"
if [ ! -f "$ELF" ]; then
  echo "error: $ELF not found -- build with:" >&2
  echo "       cmake -S . -B build-wiiu-net -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake \\" >&2
  echo "         -DBUILD_NETWORK_HARNESS=ON -DBUILD_LOCAL_SESSION_RUNNER=ON" >&2
  echo "       make -C build-wiiu-net local_session_runner -j8" >&2
  exit 1
fi
if [ ! -f "$ASSETS/spawn.mpq" ]; then
  echo "error: $ASSETS/spawn.mpq not found" >&2
  echo "       set WIIU_ASSETS_DIR to a built assets directory that has it" >&2
  exit 1
fi

# Cemu 2.6.0 takes `tweq` traps even when the condition is false; see
# cemu-tweq-workaround.py. Must run before elf2rpl.
python3 "$REPO/tools/wiiu/cemu-tweq-workaround.py" "$ELF" | tail -3

"$ELF2RPL" "$ELF" "$BUILD/local_session_runner.rpx" >/dev/null 2>&1 \
  || { echo "error: elf2rpl failed (is the ELF unstripped?)" >&2; exit 1; }

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/wiiu-lsr-wuhb.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/assets"
cp -R "$ASSETS/." "$STAGE/assets/"

ARGS=( "$BUILD/local_session_runner.rpx" "$OUT" --content="$STAGE" \
       --name "DevilutionX Local Session" --author "Devilution Team" )
if [ -f "$ICON" ]; then
  ARGS+=( --icon="$ICON" )
fi

"$WUHBT" "${ARGS[@]}" >/dev/null 2>&1 || { echo "error: wuhbtool failed" >&2; exit 1; }

echo "rpl:  $(stat -f%z "$BUILD/local_session_runner.rpx" 2>/dev/null || stat -c%s "$BUILD/local_session_runner.rpx") bytes"
echo "wuhb: $OUT ($(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") bytes)"