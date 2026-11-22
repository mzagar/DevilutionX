#!/bin/bash
# Wii U: configure (first run) -> build -> relink unstripped -> tweq patch ->
# package (Aroma-layout WUHB) -> run in Cemu -> dump log.
#
# Usage: tools/wiiu/wiiu-run.sh [seconds_to_run] [--no-build] [--no-run]
#
#   --no-build  skip compiling (package the existing ELF/WUHB)
#   --no-run    stop after packaging; do not launch Cemu (used by
#               tools/wiiu/build-release.sh)
#
# Environment overrides:
#   WIIU_BUILD_DIR  build directory   (default: <repo>/build-wiiu-release)
#   WIIU_CEMU       Cemu binary       (default: /Applications/Cemu.app/Contents/MacOS/Cemu)
#   WIIU_MLC         Cemu MLC path    (optional; default: Cemu's own setting)
#   WIIU_JOBS        parallel jobs    (default: 4)
#   DEVKITPRO       devkitPro root    (default: /opt/devkitpro)
#
# Notes:
#  * elf2rpl needs the .rela.* sections, so the ELF must be unstripped. The
#    CMake POST_BUILD strip is patched out here. A cmake re-run restores it,
#    and an already stripped ELF is relinked from link.txt.
#  * The tweq patch must be applied to the ELF before elf2rpl, see
#    cemu-tweq-workaround.py.
set -u
SECS=${1:-40}
DO_BUILD=1
DO_RUN=1
for arg in "$@"; do
  case "$arg" in
    --no-build) DO_BUILD=0 ;;
    --no-run) DO_RUN=0 ;;
  esac
done

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-release}
LOG="$HOME/Library/Application Support/Cemu/log.txt"
CEMU=${WIIU_CEMU:-/Applications/Cemu.app/Contents/MacOS/Cemu}
MLC=${WIIU_MLC:-}
JOBS=${WIIU_JOBS:-4}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
CMAKE=${CMAKE:-$(command -v cmake || true)}
READELF="$DEVKITPRO/devkitPPC/bin/powerpc-eabi-readelf"

[ -n "$CMAKE" ] || { echo "error: cmake not found in PATH" >&2; exit 1; }

# Fresh clone: configure the build directory once.
if [ ! -f "$BUILD/CMakeCache.txt" ]; then
  echo "configuring $BUILD ..."
  "$CMAKE" -S "$REPO" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" \
    -DCMAKE_BUILD_TYPE=Release || exit 1
fi

cd "$BUILD" || exit 1

if [ "$DO_BUILD" = 1 ]; then
  make -j"$JOBS" > /tmp/wiiu-make.log 2>&1 || {
    echo "BUILD FAILED:"
    grep -E "error:|Error [0-9]" /tmp/wiiu-make.log | head -15
    exit 1
  }
  grep -E "Linking CXX executable|Built target devilutionx$" /tmp/wiiu-make.log | tail -2
  [ -f devilutionx.elf ] || { echo "BUILD FAILED: no ELF"; exit 1; }
fi

# elf2rpl needs .rela sections, which the POST_BUILD strip removes.
if [ -f devilutionx.elf ] && ! "$READELF" -S devilutionx.elf 2>/dev/null | grep -q "\.rela"; then
  grep -q 'SKIP_STRIP' CMakeFiles/devilutionx.dir/build.make || {
    sed -i.bak 's|'"$DEVKITPRO"'/devkitPPC/bin/powerpc-eabi-strip|echo "SKIP_STRIP" #|' CMakeFiles/devilutionx.dir/build.make
    rm -f CMakeFiles/devilutionx.dir/build.make.bak
  }
  rm -f devilutionx.elf
  "$CMAKE" -E cmake_link_script CMakeFiles/devilutionx.dir/link.txt --verbose=0 > /dev/null || {
    echo "RELINK FAILED"
    exit 1
  }
  echo "relinked unstripped ELF"
fi

python3 "$REPO/tools/wiiu/cemu-tweq-workaround.py" devilutionx.elf | tail -3

WUHB=$BUILD/devilutionx.wuhb
WIIU_BUILD_DIR="$BUILD" "$REPO/tools/wiiu/package-wuhb.sh" "$WUHB" || { echo "PACKAGING FAILED"; exit 1; }

if [ "$DO_RUN" = 0 ]; then
  echo "skipping the Cemu launch (--no-run)"
  exit 0
fi

CEMU_ARGS=(-g "$WUHB")
if [ -n "$MLC" ]; then
  CEMU_ARGS+=(-m "$MLC")
fi
"$CEMU" "${CEMU_ARGS[@]}" > /tmp/cemu_run.log 2>&1 &
sleep "$SECS"
CPID=$(pgrep -f "Cemu.app/Contents/MacOS/Cemu" | head -1)
if [ -n "$CPID" ]; then echo "cemu still running (pid $CPID) -> killed"; kill "$CPID" 2>/dev/null; sleep 2; kill -9 "$CPID" 2>/dev/null; else echo "cemu EXITED on its own (crash?)"; fi

echo "=== CEMU LOG (tail 45) ==="
tail -45 "$LOG"
