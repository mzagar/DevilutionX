#!/bin/bash
# Extracts the game data MPQs from the GOG "Diablo + Hellfire" installer.
#
# Produces, in <outdir>:
#   DIABDAT.MPQ      (~493 MiB)  base game data
#   hellfire.mpq     (~62 MiB)   Hellfire data
#   hfmonk.mpq       (~36 MiB)   Hellfire Monk class
#   hfmusic.mpq      (~33 MiB)   Hellfire music
#   hfvoice.mpq      (~36 MiB)   Hellfire voice lines
#
# Copy all five next to the WUHB on the SD card:
#   sd:/wiiu/apps/devilutionx/
# or next to the executable on any other platform.
#
# Patch_rt.mpq / DRTL_IX86_108_109.mpq are NOT needed: DevilutionX has its own
# fixes and does not read them.
#
# Usage: tools/wiiu/extract-gog-mpqs.sh <installer.exe> [outdir]
#
# The GOG installer is an Inno Setup archive, so `innoextract` is required.
# If it is not installed locally, this script falls back to running it in a
# throwaway Docker container.
set -eu

INSTALLER=${1:-}
OUTDIR=${2:-./gog-mpq}
if [ -z "$INSTALLER" ] || [ ! -f "$INSTALLER" ]; then
  echo "usage: $0 <gog-installer.exe> [outdir]" >&2
  exit 1
fi
INSTALLER=$(cd "$(dirname "$INSTALLER")" && pwd)/$(basename "$INSTALLER")
mkdir -p "$OUTDIR"
OUTDIR=$(cd "$OUTDIR" && pwd)

# The GOG Hellfire MPQs live in a `hellfire/` subdirectory of the installer;
# the base game data sits at the root. Flatten them into one directory.
FILTERS=(
  -I "DIABDAT.MPQ"
  -I "hellfire/hellfire.mpq"
  -I "hellfire/hfmonk.mpq"
  -I "hellfire/hfmusic.mpq"
  -I "hellfire/hfvoice.mpq"
)

if command -v innoextract >/dev/null 2>&1; then
  echo "using local innoextract"
  innoextract -e "${FILTERS[@]}" -d "$OUTDIR" "$INSTALLER"
else
  command -v docker >/dev/null 2>&1 || {
    echo "error: neither innoextract nor docker found" >&2
    echo "       install innoextract, or run: docker run --rm -v ... " >&2
    exit 1
  }
  echo "using innoextract via docker"
  # postgres is only used as a convenient Debian image that is usually already
  # present locally; any Debian/Ubuntu image works.
  IMAGE=${WIIU_DOCKER_IMAGE:-postgres:15.1}
  docker run --rm \
    -v "$(dirname "$INSTALLER"):/in:ro" \
    -v "$OUTDIR:/out" \
    "$IMAGE" \
    bash -c 'apt-get update -qq >/dev/null 2>&1 || true;
             apt-get install -y -qq innoextract >/dev/null 2>&1;
             innoextract -e '"${FILTERS[*]}"' -d /out "/in/'"$(basename "$INSTALLER")"'"'
fi

# Flatten hellfire/*.mpq and clean up the empty subdirectory.
if [ -d "$OUTDIR/hellfire" ]; then
  mv "$OUTDIR"/hellfire/*.mpq "$OUTDIR"/
  rmdir "$OUTDIR/hellfire"
fi

echo
echo "extracted into $OUTDIR:"
ls -l "$OUTDIR"/*.mpq

missing=0
for f in DIABDAT.MPQ hellfire.mpq hfmonk.mpq hfmusic.mpq hfvoice.mpq; do
  if [ ! -s "$OUTDIR/$f" ]; then
    echo "MISSING: $f" >&2
    missing=1
  fi
done
[ "$missing" = 0 ] || exit 1

echo
echo "next: copy these into sd:/wiiu/apps/devilutionx/ (next to the .wuhb)"
