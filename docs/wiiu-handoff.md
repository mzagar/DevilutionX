# Wii U Port — Handoff

> **Role of this file:** where to resume. It answers *"what is the state, what do
> I do next, and how do I run it"*. For *how it works and why*, see
> [`wiiu-progress.md`](wiiu-progress.md).

## Where we are

Diablo (Shareware) **runs and is playable on Cemu 2.6.0 on macOS** — display,
assets, keyboard/mouse, text input, audio (SFX + music), gameplay and savegames
all work.

The port is developed **on Cemu only**; real hardware (Aroma) testing is
deliberately deferred until the artifact is packaged properly.

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
- Assets loaded from the emulated SD card (`/vol/external01`)
- Keyboard/mouse — works once mapped to Controller 1 (GamePad) in Cemu input settings
- Text input — `keyboard.cpp` bypasses the broken system swkbd
- **Audio** — `NOSOUND=OFF`, SDL2 Wii U AX driver + bundled SDL_audiolib
- Lua — `LUA_USE_C89` on PowerPC
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

## 🎯 Next task: Aroma-layout packaging

**Goal:** a self-contained WUHB that behaves identically on Cemu and on Aroma —
assets inside `/content/`, nothing written to the SD card root, correct
meta/icon.

Current situation:

- Assets live *next to* the WUHB on the SD card root, not inside it.
- `Source/platform/wiiu/init.cpp` is an empty no-op; nothing calls
  `paths::SetBasePath()`. All paths come from SDL (`Source/utils/paths.cpp`):
  `BasePath()` = `SDL_GetBasePath()`, `AssetsPath()` = `BasePath() + "assets/"`,
  and `PrefPath()` is forced to `""` because a writable `diablo.ini` exists in
  the CWD — i.e. the game currently **writes saves/config next to the WUHB**.
- This is inferred from behaviour, not logged.

Steps:

1. Log `SDL_GetBasePath()`, `BasePath()`, `AssetsPath()`, `PrefPath()`,
   `ConfigPath()` via OSReport to get ground truth on Cemu.
2. Decide the layout: assets in `/content/` inside the WUHB, and figure out how
   Aroma exposes that (`/vol/content`, a bind mount, or the app's own
   filesystem). Check how the Switch port does it (`romfs:/` in
   `Source/utils/paths.cpp`, `switch_romfs_init()` in `Source/main.cpp`).
3. Give `wiiu_init()` a real purpose — set base/assets/pref paths explicitly
   instead of relying on SDL's defaults.
4. Package with `wuhbtool` including `/content/` + meta (`--name`, `--author`,
   icon), and verify on Cemu that it behaves the same as the SD-root layout.
5. Only then test on real Aroma hardware.

## Key files

| File | Role |
|------|------|
| `Source/platform/wiiu/init.cpp` | `wiiu_init()` — currently empty; packaging work goes here |
| `Source/platform/wiiu/keyboard.cpp` | Text input, bypasses system swkbd |
| `Source/utils/paths.cpp` | Base/assets/pref path resolution |
| `Source/main.cpp` | Calls `wiiu_init()` |
| `CMake/platforms/wiiu.cmake` | Platform build config (`NONET=ON`, sound on) |
| `3rdParty/Lua/CMakeLists.txt` | `WIIU` in the `LUA_USE_C89` list |
| `tools/wiiu/wiiu-run.sh` | Build → package → run → log |
| `tools/wiiu/cemu-tweq-workaround.py` | Cemu `tweq` workaround |
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
| Assets source | `build-wiiu-clean/assets/` |

## Notes for whoever continues

- **Keep the debug logging** — it is the primary debugging tool for this port.
- `Source/engine/sound.cpp` still has two throwaway test blocks (an atomic test
  and a `__builtin_trap` test) from the `tweq` investigation. Harmless, but
  remove them before an upstream PR.
- OSReport's printf does **not** support `%zu`/`%lu` — use `%d` with casts.
- Many repo files are CRLF in git (`.gitattributes` has `* -text`) — don't let
  an editor rewrite line endings, or you get a whole-file diff.
