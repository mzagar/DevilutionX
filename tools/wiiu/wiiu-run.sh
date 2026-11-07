#!/bin/bash
# Wii U: build -> tweq patch -> package (Aroma-layout WUHB) -> run in Cemu -> dump log.
#
# Usage: tools/wiiu/wiiu-run.sh [seconds_to_run] [--no-build]
#
# Environment overrides:
#   WIIU_BUILD_DIR  build directory        (default: <repo>/build-wiiu-release)
#   WIIU_CEMU       Cemu binary            (default: /Applications/Cemu.app/Contents/MacOS/Cemu)
#   WIIU_MLC        Cemu MLC path          (default: /Users/mario/work/wiiu/cemu-mlc)
#   DEVKITPRO       devkitPro root         (default: /opt/devkitpro)
#
# Notes:
#  * The strip patch is required for elf2rpl (stripping removes the .rela.*
#    sections elf2rpl needs) and is lost on every `cmake` re-run, so it is
#    re-applied here.
#  * The tweq patch must be applied to the ELF before elf2rpl, see
#    cemu-tweq-workaround.py.
set -u
SECS=${1:-40}
DO_BUILD=1
[ "${2:-}" = "--no-build" ] && DO_BUILD=0

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-release}
LOG="$HOME/Library/Application Support/Cemu/log.txt"
CEMU=${WIIU_CEMU:-/Applications/Cemu.app/Contents/MacOS/Cemu}
MLC=${WIIU_MLC:-/Users/mario/work/wiiu/cemu-mlc}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}

cd "$BUILD" || exit 1

if [ "$DO_BUILD" = 1 ]; then
  make -j4 > /tmp/wiiu-make.log 2>&1 || {
    echo "BUILD FAILED:"
    grep -E "error:|Error [0-9]" /tmp/wiiu-make.log | head -15
    exit 1
  }
  grep -E "Linking CXX executable|Built target devilutionx$" /tmp/wiiu-make.log | tail -2
  [ -f devilutionx.elf ] || { echo "BUILD FAILED: no ELF"; exit 1; }
  # re-apply strip patch (lost on cmake re-run)
  grep -q 'SKIP_STRIP' CMakeFiles/devilutionx.dir/build.make || \
    sed -i '' 's|'"$DEVKITPRO"'/devkitPPC/bin/powerpc-eabi-strip|echo "SKIP_STRIP" #|' CMakeFiles/devilutionx.dir/build.make
fi

python3 "$REPO/tools/wiiu/cemu-tweq-workaround.py" devilutionx.elf | tail -3

WUHB=$BUILD/devilutionx.wuhb
WIIU_BUILD_DIR="$BUILD" "$REPO/tools/wiiu/package-wuhb.sh" "$WUHB" || { echo "PACKAGING FAILED"; exit 1; }

"$CEMU" -g "$WUHB" -m "$MLC" > /tmp/cemu_run.log 2>&1 &
sleep "$SECS"
CPID=$(pgrep -f "Cemu.app/Contents/MacOS/Cemu" | head -1)
if [ -n "$CPID" ]; then echo "cemu still running (pid $CPID) -> killed"; kill "$CPID" 2>/dev/null; sleep 2; kill -9 "$CPID" 2>/dev/null; else echo "cemu EXITED on its own (crash?)"; fi

echo "=== CEMU LOG (tail 45) ==="
tail -45 "$LOG"
