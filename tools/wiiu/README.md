# Wii U port tooling

| Command | What it does |
|---|---|
| `make dist` (or `tools/wiiu/build-release.sh`) | Fresh-clone friendly: configures, builds, packs the Hellfire mod, produces the WUHB and a ready-to-extract `dist/devilutionx-wiiu-<branch>-<sha>.zip`. No Cemu needed. |
| `tools/wiiu/wiiu-run.sh [secs]` | The Cemu dev loop: build → tweq patch → package → launch Cemu → dump the log. |
| `tools/wiiu/wiiu-run.sh [secs] --no-build` | Re-package and re-run without compiling. |
| `tools/wiiu/wiiu-run.sh [secs] --no-run` | Build and package, do not launch Cemu. |
| `tools/wiiu/package-wuhb.sh [out.wuhb]` | Packages an existing `build-wiiu-release/devilutionx.rpx` + assets into a WUHB. |
| `tools/wiiu/build-hf-mpq.sh [out.mpq]` | Packs the Hellfire mod (`mods/hf` → `mods/hf.mpq`); needs `smpq` or Docker. |
| `tools/wiiu/extract-gog-mpqs.sh <installer.exe> <dir>` | Extracts `DIABDAT.MPQ`, `hellfire.mpq`, `hfmonk/hfmusic/hfvoice.mpq` from the GOG installer. |
| `tools/wiiu/cemu-run.sh <game.wuhb>` | Launches Cemu with SIGPIPE ignored (workaround for a Cemu 2.6.0 macOS bug). |

## Requirements for `make dist`

devkitPro with the Wii U toolchain (`devkitPPC`, `wut`, `wiiu-cmake`,
`wiiu-sdl2`, `wut-tools`), CMake ≥ 3.22, `make`, `zip`, and `smpq` or Docker
for the Hellfire mod. `spawn.mpq` (shareware) is downloaded automatically on
the first build.

Environment overrides: `WIIU_BUILD_DIR`, `WIIU_DIST_DIR`, `WIIU_APP_DIR_NAME`,
`WIIU_APP_NAME`, `WIIU_JOBS`, `DEVKITPRO`, `WIIU_HF_MPQ`.

## Two builds on one SD card

The writable directory is `sd:/wiiu/apps/<WIIU_APP_DIR_NAME>/`
(default `devilutionx`). A second build can coexist:

```sh
cmake -S . -B build-wiiu-other \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake \
  -DWIIU_APP_DIR_NAME=devilutionx-other
```

and package it with `WIIU_APP_NAME="DevilutionX Other"` so Aroma lists both.
