#!/bin/bash
# Packs the Hellfire mod (mods/hf/) into mods/hf.mpq.
#
# Why this is needed: DevilutionX loads the Hellfire expansion as a *mod*.
#   - As a loose directory (mods/hf/) the mod registers no identifier, so its
#     manifest `saveExtension=hsv` is ignored and Hellfire saves land in the
#     same namespace as Diablo saves (single_*.sv).
#   - As a packed MPQ (mods/hf.mpq) the manifest is read, so Hellfire gets its
#     own save namespace (single_*.hsv) -- matching desktop/distro builds.
#
# The file list is taken from CMake/Mods.cmake so the MPQ matches what upstream
# builds (note: txtdata/sound/effects-unused.tsv is deliberately excluded).
#
# Usage: tools/wiiu/build-hf-mpq.sh [output.mpq]
#
# Environment overrides:
#   WIIU_DOCKER_IMAGE  image used when smpq is not installed locally
#                      (default: debian:11)
set -eu

REPO=$(cd "$(dirname "$0")/../.." && pwd)
MOD_DIR="$REPO/mods/hf"
OUT=${1:-$REPO/build-wiiu-release/mods/hf.mpq}
DOCKER_IMAGE=${WIIU_DOCKER_IMAGE:-ubuntu:22.04}

[ -d "$MOD_DIR" ] || { echo "error: $MOD_DIR not found" >&2; exit 1; }

# Extract the hellfire_mod file list from CMake/Mods.cmake (between
# `set(hellfire_mod` and the closing paren), then strip indentation.
FILES=$(tr -d '\r' < "$REPO/CMake/Mods.cmake" \
  | sed -n '/^set(hellfire_mod/,/)$/p' \
  | sed '1d' \
  | sed 's/)//' \
  | tr -d ' \t' \
  | grep -v '^$')
# data/inv/objcurs2-widths.txt is appended conditionally (only when NOT UNPACKED_MPQS).
FILES=$(printf '%s\ndata/inv/objcurs2-widths.txt\n' "$FILES")
echo "files:   $(printf '%s\n' "$FILES" | grep -c .) from CMake/Mods.cmake"

missing=0
while IFS= read -r f; do
  [ -f "$MOD_DIR/$f" ] || { echo "error: missing $f" >&2; missing=1; }
done <<< "$FILES"
[ "$missing" = 0 ] || exit 1

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

# shellcheck disable=SC2086
run_smpq() {
  # smpq -A  add files           -M 1  MPQ format v1
  #       -C BZIP2  compression  -c    create
  smpq -A -M 1 -C BZIP2 -c "$1" $FILES
}

if command -v smpq >/dev/null 2>&1; then
  (cd "$MOD_DIR" && run_smpq "$OUT")
else
  command -v docker >/dev/null 2>&1 || {
    echo "error: neither smpq nor docker is available" >&2; exit 1; }
  # Reuse a tiny image with smpq preinstalled so repeat runs are fast.
  HELPER_IMAGE=${WIIU_SMPQ_IMAGE:-wiiu-smpq:latest}
  if ! docker image inspect "$HELPER_IMAGE" >/dev/null 2>&1; then
    echo "smpq not installed locally; building helper image from $DOCKER_IMAGE"
    docker build -t "$HELPER_IMAGE" - <<EOF >/dev/null
FROM $DOCKER_IMAGE
RUN apt-get update -qq && apt-get install -y -qq smpq && rm -rf /var/lib/apt/lists/*
EOF
  fi
  # Newlines would be command separators inside `bash -c`; join with spaces.
  FILES_ONE_LINE=$(printf '%s' "$FILES" | tr '\n' ' ')
  docker run --rm -v "$MOD_DIR":/mod:ro -v "$(dirname "$OUT")":/out \
    "$HELPER_IMAGE" \
    bash -c "cd /mod && smpq -A -M 1 -C BZIP2 -c '/out/$(basename "$OUT")' $FILES_ONE_LINE"
fi

[ -f "$OUT" ] || { echo "error: smpq produced no output" >&2; exit 1; }
echo "hf.mpq:  $OUT ($(stat -f%z "$OUT" 2>/dev/null || stat -c%s "$OUT") bytes)"
