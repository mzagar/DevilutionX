#!/bin/bash
# Builds a distributable Wii U release ZIP from a fresh clone, without Cemu.
#
#   tools/wiiu/build-release.sh            # or: make dist
#
# Output: <repo>/dist/devilutionx-wiiu-<branch>-<sha>.zip containing
#   wiiu/apps/<appdir>/devilutionx.wuhb   self-contained shareware build
#   README.txt, FULL-GAME.txt, BUILD-INFO.txt, LICENSE.md
#
# The WUHB embeds the engine, the Hellfire mod (mods/hf.mpq) and the Diablo
# SHAREWARE data (spawn.mpq, downloaded once). No retail game data is included.
#
# Requirements: devkitPro with the Wii U toolchain (devkitPPC, wut,
# wiiu-cmake, wiiu-sdl2, wut-tools), CMake >= 3.22, make, zip, and (for the
# Hellfire mod) smpq or Docker.
#
# Environment overrides:
#   WIIU_BUILD_DIR     build directory        (default: <repo>/build-wiiu-release)
#   WIIU_DIST_DIR      output directory       (default: <repo>/dist)
#   WIIU_APP_DIR_NAME  SD directory name      (default: this branch's value)
#   WIIU_APP_NAME      Aroma menu name        (default: this branch's value)
#   WIIU_JOBS          parallel build jobs    (default: CPU count)
#   DEVKITPRO          devkitPro root         (default: /opt/devkitpro)
#   WIIU_HF_MPQ        prebuilt Hellfire mod  (default: <build>/mods/hf.mpq)
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${WIIU_BUILD_DIR:-$REPO/build-wiiu-release}
DIST=${WIIU_DIST_DIR:-$REPO/dist}
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}

# Branch-specific defaults (this file differs per branch on purpose).
BRANCH_LABEL=${WIIU_BRANCH_LABEL:-wiiu}
DEFAULT_APP_DIR=${WIIU_DEFAULT_APP_DIR:-devilutionx}
DEFAULT_APP_NAME=${WIIU_DEFAULT_APP_NAME:-DevilutionX}
TAGLINE=${WIIU_TAGLINE:-regular port}

APP_DIR=${WIIU_APP_DIR_NAME:-$DEFAULT_APP_DIR}
APP_NAME=${WIIU_APP_NAME:-$DEFAULT_APP_NAME}

JOBS=${WIIU_JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) )}
export WIIU_JOBS="$JOBS"

echo "== DevilutionX Wii U release build =="
echo "branch:   $BRANCH_LABEL"
echo "app:      $APP_NAME  (sd:/wiiu/apps/$APP_DIR/)"
echo "build:    $BUILD"
echo "output:   $DIST"
echo

if [ ! -d "$DEVKITPRO/wut" ] || [ ! -x "$DEVKITPRO/tools/bin/elf2rpl" ]; then
  echo "error: devkitPro with the Wii U toolchain not found at $DEVKITPRO" >&2
  echo "       install devkitPPC, wut, wiiu-cmake, wiiu-sdl2, wut-tools" >&2
  echo "       or point DEVKITPRO at your devkitPro root" >&2
  exit 1
fi

# --- 1. Hellfire mod ---------------------------------------------------------
HF_MPQ=${WIIU_HF_MPQ:-$BUILD/mods/hf.mpq}
if [ ! -f "$HF_MPQ" ]; then
  echo "-- packing the Hellfire mod (mods/hf -> $HF_MPQ)"
  WIIU_BUILD_DIR="$BUILD" "$REPO/tools/wiiu/build-hf-mpq.sh" "$HF_MPQ" || {
    echo "warning: could not pack hf.mpq; the build will ship without the Hellfire mod" >&2
    echo "         install smpq or Docker and re-run for a full package" >&2
  }
fi

# --- 2. configure + build + tweq + package (shared with the Cemu dev loop) ---
echo "-- building the WUHB"
WIIU_BUILD_DIR="$BUILD" \
WIIU_APP_DIR_NAME="$APP_DIR" \
WIIU_APP_NAME="$APP_NAME" \
WIIU_APP_SHORT_NAME="$APP_NAME" \
WIIU_HF_MPQ="$HF_MPQ" \
  "$REPO/tools/wiiu/wiiu-run.sh" 0 --no-run

WUHB=$BUILD/devilutionx.wuhb
[ -f "$WUHB" ] || { echo "error: $WUHB was not produced" >&2; exit 1; }

# --- 3. release zip ----------------------------------------------------------
SHA=$(git -C "$REPO" rev-parse --short=9 HEAD 2>/dev/null || echo unknown)
SUBJECT=$(git -C "$REPO" log -1 --pretty=%s 2>/dev/null || echo unknown)
mkdir -p "$DIST"
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/wiiu-release.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/wiiu/apps/$APP_DIR"
cp "$WUHB" "$STAGE/wiiu/apps/$APP_DIR/devilutionx.wuhb"
[ -f "$REPO/LICENSE.md" ] && cp "$REPO/LICENSE.md" "$STAGE/LICENSE.md"

cat > "$STAGE/README.txt" <<EOF
DevilutionX for the Wii U - $TAGLINE
================================================================

WHAT THIS IS
------------
Diablo running natively on the Wii U through the DevilutionX source port.
The WUHB contains the engine, the Hellfire expansion code (as a mod) and the
Diablo SHAREWARE game data (spawn.mpq), so it runs out of the box.

REQUIREMENT
-----------
A Wii U with Aroma custom firmware.

INSTALLATION
------------
1. Extract this archive at the root of the SD card, so that the WUHB ends up
   at:

       sd:/wiiu/apps/$APP_DIR/devilutionx.wuhb

2. Insert the SD card, boot into Aroma, open the Homebrew Launcher and start
   "$APP_NAME".

This build keeps everything (saves, diablo.ini, screenshots, retail data) in
sd:/wiiu/apps/$APP_DIR/. A second DevilutionX build installed next to it uses
its own directory and never touches these files.

WHAT YOU GET
------------
* Diablo SHAREWARE (the freely distributable demo campaign).
* No intro cinematic and no Hellfire campaign: both need the retail data
  files, which are not included.

FULL GAME (Diablo + Hellfire)
-----------------------------
See FULL-GAME.txt: copy five files from your own legally purchased copy next
to the WUHB and this same installation plays the full campaign, with the
intro cinematics and Hellfire.

BUILD INFORMATION
-----------------
See BUILD-INFO.txt for the exact source revision and hashes.

CREDITS / LICENSE
-----------------
DevilutionX is free software; see LICENSE.md and
https://github.com/diasurgical/devilutionX
Diablo and Hellfire are trademarks of Blizzard Entertainment. The shareware
data inside the WUHB is the freely redistributable Diablo shareware release.
EOF

cat > "$STAGE/FULL-GAME.txt" <<EOF
Playing the FULL game (Diablo + Hellfire) with your own data
===========================================================

This package ships the DevilutionX engine with the Diablo shareware data only.
No retail Blizzard game data is included and none can be redistributed: the
retail MPQ files are copyrighted, so every user supplies them from their own
legally purchased copy.

1. FILES TO COPY
----------------
Install the package as described in README.txt (the WUHB must end up at
sd:/wiiu/apps/$APP_DIR/devilutionx.wuhb), then copy these FIVE files from your
own Diablo + Hellfire installation into the SAME directory, next to the WUHB:

    sd:/wiiu/apps/$APP_DIR/DIABDAT.MPQ    (493 MiB)
    sd:/wiiu/apps/$APP_DIR/hellfire.mpq   ( 62 MiB)
    sd:/wiiu/apps/$APP_DIR/hfmonk.mpq     ( 36 MiB)
    sd:/wiiu/apps/$APP_DIR/hfmusic.mpq    ( 33 MiB)
    sd:/wiiu/apps/$APP_DIR/hfvoice.mpq    ( 36 MiB)

Use exactly these names. The SD card is FAT, so the lookup is
case-insensitive, but keep them as above.

2. FILES THAT ARE NOT NEEDED
----------------------------
* Patch_rt.mpq            - not needed, DevilutionX has its own fixes
* DRTL_IX86_108_109.mpq   - not needed
* No "mods" folder: the Hellfire expansion code is already packaged inside the
  WUHB (assets/mods/hf.mpq). Only the Hellfire DATA mpqs above are required.
* Optional: hfbard.mpq and hfbarb.mpq (Bard/Barbarian). GOG does not ship
  them; without them those two classes are simply unavailable.

3. WHERE THE FILES COME FROM (GOG INSTALLER)
--------------------------------------------
The easiest legal source is the "Diablo + Hellfire" bundle from GOG. Its
installer is an Inno Setup archive, so extract it with innoextract:

    innoextract -e -I "DIABDAT.MPQ" \\
                   -I "hellfire/hellfire.mpq" \\
                   -I "hellfire/hfmonk.mpq" \\
                   -I "hellfire/hfmusic.mpq" \\
                   -I "hellfire/hfvoice.mpq" \\
                   -d <output dir> setup_diablo_hellfire_*.exe

The repository has a helper script that does exactly this:

    tools/wiiu/extract-gog-mpqs.sh <installer.exe> <output dir>

4. FIRST LAUNCH WITH RETAIL DATA
--------------------------------
With the five files present, on the next start the game detects the retail
data and shows the "Enter Hellfire" / "Switch to Diablo" chooser (the game
mode option defaults to "Ask"), then plays the intro cinematic once (the intro
option defaults to "Once"). Both can be changed later in Settings.

5. TROUBLESHOOTING
------------------
* "diabdat.mpq or spawn.mpq" prompt: the data files are not where the engine
  looks. They must be in the SAME folder as the WUHB.
* "Some Hellfire MPQs are missing": hellfire.mpq, hfmonk.mpq, hfmusic.mpq and
  hfvoice.mpq must all be present together.
* Class list without the Monk: the Hellfire mod did not load. Check that the
  WUHB embeds assets/mods/hf.mpq.

Diablo and Hellfire are trademarks of Blizzard Entertainment. You must own the
games to use this package with retail data.
EOF

SPAWN=$BUILD/assets/spawn.mpq
WUHB_SHA=$(shasum -a 256 "$WUHB" | cut -d' ' -f1)
SPAWN_SHA=$([ -f "$SPAWN" ] && shasum -a 256 "$SPAWN" | cut -d' ' -f1 || echo "n/a")
HF_SHA=$([ -f "$HF_MPQ" ] && shasum -a 256 "$HF_MPQ" | cut -d' ' -f1 || echo "n/a")

cat > "$STAGE/BUILD-INFO.txt" <<EOF
DevilutionX for the Wii U - build information
=============================================

Source
  repository   $(git -C "$REPO" config --get remote.origin.url 2>/dev/null || echo unknown)
  branch       $BRANCH_LABEL
  commit       $(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo unknown)  ($SHA)
  subject      $SUBJECT
  built at     $(date -u '+%Y-%m-%d %H:%M UTC')
  built with   tools/wiiu/build-release.sh

Toolchain
  devkitPPC    $(powerpc-eabi-gcc --version 2>/dev/null | head -1 || echo unknown)
  devkitPro    $DEVKITPRO
  build type   Release (DISABLE_LTO)
  SD data dir  sd:/wiiu/apps/$APP_DIR/   (WIIU_APP_DIR_NAME=$APP_DIR)

Artifacts
  devilutionx.wuhb  $( (stat -f%z "$WUHB" 2>/dev/null || stat -c%s "$WUHB") ) bytes
                    sha256 $WUHB_SHA
  embedded assets/spawn.mpq
                    sha256 $SPAWN_SHA
  embedded assets/mods/hf.mpq
                    sha256 $HF_SHA

This package contains no retail Blizzard game data.
EOF

OUT_ZIP=$DIST/devilutionx-$BRANCH_LABEL-$SHA.zip
rm -f "$OUT_ZIP"
( cd "$STAGE" && zip -qr -X "$OUT_ZIP" . )

echo
echo "== done =="
echo "zip:     $OUT_ZIP ($(du -h "$OUT_ZIP" | cut -f1))"
echo "sha256:  $(shasum -a 256 "$OUT_ZIP" | cut -d' ' -f1)"
echo "wuhb:    $(du -h "$WUHB" | cut -f1), sha256 $WUHB_SHA"
echo
echo "Extract the zip at the root of an Aroma SD card and launch \"$APP_NAME\"."
