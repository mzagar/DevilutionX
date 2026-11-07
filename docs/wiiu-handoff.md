# Wii U Port — Handoff

> **Role of this file:** where to resume. It answers *"what is the state, what do
> I do next, and how do I run it"*. For *how it works and why*, see
> [`wiiu-progress.md`](wiiu-progress.md).

## Where we are

Diablo (Shareware) **runs and is playable on Cemu 2.6.0 on macOS, from a
self-contained Aroma-layout WUHB** — display, assets, keyboard/mouse, text input,
audio (SFX + music), gameplay and savegames all work, and nothing is written to
the SD card root.

The port is developed **on Cemu only**; real hardware (Aroma) testing is
deliberately deferred until the artifact is packaged properly — which it now is.

## ⚡ Quick start

```bash
cd /Users/mario/work/wiiu/DevilutionX
tools/wiiu/wiiu-run.sh 40              # build → patch → package → run → log
tools/wiiu/wiiu-run.sh 40 --no-build   # re-run without rebuilding
```

Log:

```bash
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

## ✅ Done

- Display / SDL-GX2 renderer, main menu and in-game rendering
- Keyboard/mouse — works once mapped to Controller 1 (GamePad) in Cemu input settings
- Text input — `keyboard.cpp` bypasses the broken system swkbd
- **Audio** — `NOSOUND=OFF`, SDL2 Wii U AX driver + bundled SDL_audiolib
- Lua — `LUA_USE_C89` on PowerPC
- **Aroma-layout packaging** — assets inside the WUHB (`/vol/content/assets`),
  writable data in `sd:/wiiu/apps/devilutionx/`, nothing in the SD card root.
  `tools/wiiu/package-wuhb.sh` builds a self-contained ~41 MB WUHB.
- Build/package pipeline + the Cemu `tweq` workaround, both scripted in `tools/wiiu/`

## ⚠️ Two things that will bite you

1. **The Cemu `tweq` bug.** Cemu 2.6.0 takes `tweq` traps even when the
   condition is false, and GCC emits `tweq` for libstdc++'s shared_ptr
   use-count guard — so **any shared_ptr copy hangs the guest**. The symptom is
   a silent hang: the log stops mid-line and Cemu sits at ~200 % CPU. Always run
   `tools/wiiu/cemu-tweq-workaround.py` on `devilutionx.elf` **before**
   `elf2rpl`, and re-run it after every rebuild. Full story:
   [`wiiu-progress.md`](wiiu-progress.md#1-cemus-tweq-traps-even-when-the-condition-is-false-the-big-one).

2. **`elf2rpl` needs an unstripped ELF.** The strip patch is lost on every
   `cmake` re-run; `wiiu-run.sh` re-applies it.

## 🎯 Next task: real-hardware validation

**Goal:** run the packaged WUHB on a real Wii U with Aroma and confirm it behaves
the same as on Cemu.

1. Copy `build-wiiu-release/devilutionx.wuhb` to `sd:/wiiu/apps/` on the Wii U.
2. Launch it from the Wii U Menu (Aroma) and check: main menu, input, audio,
   starting a game, saving, and that `sd:/wiiu/apps/devilutionx/` receives
   `diablo.ini` + save files.
3. Get OSReport off the console (UDP or serial) — the `WIIUINIT` lines state
   which layout was picked and what the resolved paths are.

Things to watch for:

- The `tweq` workaround is **not needed** on real hardware (it is a Cemu bug),
  but the patched binary is safe to run there anyway: the NOPped guard is an
  unreachable 2³¹-overflow check.
- `/vol/save` is the per-title save volume on real hardware and would be a
  better home for saves than the SD card. `wiiu_log_paths()` already logs
  `exists=`/`writable=` for it — check the hardware log before changing anything.

## ℹ️ How the packaging works

```
devilutionx.wuhb
└── /content/assets/…        read-only, mounted at /vol/content/assets

sd:/wiiu/apps/devilutionx/   writable, created on first run
├── diablo.ini
└── save_0.sv
```

- SDL2 for Wii U has **no filesystem backend** — `SDL_GetBasePath()` and
  `SDL_GetPrefPath()` always return `NULL`, so `wiiu_init()` resolves every path
  itself. See [`wiiu-progress.md`](wiiu-progress.md#8-sdl2-for-wii-u-has-no-filesystem-backend).
- Assets are packaged from `build-wiiu-clean/assets/` (a `BUILD_ASSETS_MPQ=ON`
  build) via `tools/wiiu/package-wuhb.sh`; the legacy SD-root layout still works
  as a fallback when no content mount is present.

## Key files

| File | Role |
|------|------|
| `Source/platform/wiiu/init.cpp` | `wiiu_init()` — environment log + path setup |
| `Source/platform/wiiu/keyboard.cpp` | Text input, bypasses system swkbd |
| `Source/utils/paths.cpp` | Base/assets/pref path resolution (SDL) |
| `Source/main.cpp` | Calls `wiiu_init()` |
| `CMake/platforms/wiiu.cmake` | Platform build config (`NONET=ON`, sound on) |
| `3rdParty/Lua/CMakeLists.txt` | `WIIU` in the `LUA_USE_C89` list |
| `tools/wiiu/wiiu-run.sh` | Build → package → run → log |
| `tools/wiiu/package-wuhb.sh` | Packages the self-contained Aroma-layout WUHB |
| `tools/wiiu/cemu-tweq-workaround.py` | Cemu `tweq` workaround |
| `Packaging/wiiu/icon.png` | 128×128 WUHB icon |
| `docs/wiiu-progress.md` | Full technical reference |

## Environment

| What | Where |
|------|-------|
| Cemu | `/Applications/Cemu.app` |
| Cemu MLC | `/Users/mario/work/wiiu/cemu-mlc` |
| Cemu log | `~/Library/Application Support/Cemu/log.txt` (rewritten each run) |
| Emulated SD card | `~/Library/Application Support/Cemu/sdcard/` → `/vol/external01` |
| devkitPro | `/opt/devkitpro` (`elf2rpl`, `wuhbtool` in `tools/bin`) |
| Build dir | `build-wiiu-release` |
| Assets source | `build-wiiu-clean/assets/` (packaged into the WUHB) |
| Data on SD | `sd:/wiiu/apps/devilutionx/` (`diablo.ini`, `save_*.sv`) |

## Notes for whoever continues

- **Keep the debug logging** — it is the primary debugging tool for this port.
- `Source/engine/sound.cpp` still has two throwaway test blocks (an atomic test
  and a `__builtin_trap` test) from the `tweq` investigation. Harmless, but
  remove them before an upstream PR.
- OSReport's printf does **not** support `%zu`/`%lu` — use `%d` with casts.
- Many repo files are CRLF in git (`.gitattributes` has `* -text`) — don't let
  an editor rewrite line endings, or you get a whole-file diff.
