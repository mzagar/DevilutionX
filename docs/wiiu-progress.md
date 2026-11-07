# Wii U Port — Progress & Technical Reference

> **Role of this file:** technical reference + history. It answers *"how does this
> work and why is it this way"*. For *"where do I resume"*, see
> [`wiiu-handoff.md`](wiiu-handoff.md).

## Status (7 Nov 2026)

**Diablo (Shareware) is fully playable on Cemu 2.6.0 on macOS, from a
self-contained Aroma-layout WUHB** — video, assets, input, text input, audio,
gameplay and savegames all work, with nothing written to the SD card root.

Not done yet: real-hardware test, network.

| Area | State | Notes |
|------|-------|-------|
| Boot / display | ✅ | SDL/GX2 renderer, main menu + in-game rendering |
| Assets | ✅ | Read from the WUHB content mount (`/vol/content/assets`) |
| Keyboard / mouse | ✅ | Works once mapped to Controller 1 (GamePad) in Cemu input settings |
| Text input | ✅ | `keyboard.cpp` bypasses the system swkbd |
| Audio (SFX + music) | ✅ | SDL2 Wii U **AX** audio driver + bundled SDL_audiolib |
| Savegames / config | ✅ | Written to `/vol/external01/wiiu/apps/devilutionx/` |
| Lua scripting | ✅ | Needs `LUA_USE_C89` on PowerPC |
| Self-contained WUHB | ✅ | ~41 MB WUHB: game + `assets/` inside `/content/` |
| Network / multiplayer | ❌ | `NONET=ON`, no `nsysnet` init yet |
| Real hardware (Aroma) | ⏳ | Untested by choice — Cemu first |

## Quick start (build → package → run)

```bash
tools/wiiu/wiiu-run.sh 40          # build, patch, package, run in Cemu, dump log
tools/wiiu/wiiu-run.sh 40 --no-build   # skip the build (fast re-run)
```

The script does the full chain, including the two steps that are easy to forget
(strip patch and `tweq` patch). See [Build pipeline](#build-pipeline).

## Major findings

### 1. Cemu's `tweq` traps even when the condition is false (the big one)

**Symptom:** the game hangs at a specific point — the guest spins forever, the
OSReport log stops mid-line, Cemu stays alive at ~200 % CPU. Nothing crashes.

**Cause:** Cemu 2.6.0 implements `tweq` (conditional trap word, TO=EQ — "trap if
equal") incorrectly: it takes the trap *even when the registers are not equal*.
Cemu runs TW through its interpreter (`PPCInterpreter_TW` appears in `sample`
output — the JIT has no TW, so it falls back), and that handler is buggy.

**Why it hit us:** GCC emits `tweq` for libstdc++'s shared_ptr use-count
overflow guard, i.e. for

```cpp
if (old_use_count == 0x7FFFFFFF) __builtin_trap();
```

which is generated on **every `shared_ptr` copy**. So on Cemu, *any* shared_ptr
copy hangs the guest. In our case the first one happened inside
`LoadAudioFile()` → `SoundSample::SetChunk()`, which is why it looked like
"audio hangs".

**Proof chain:**

1. Markers narrowed the hang to `result.SetChunk(...)` for the first sfx
   (`sfx\items\titlemov.mp3`, 418 bytes) during `ui_sound_init()`.
2. A `std::atomic<int>` test printed `atomic test done val=1` → `lwarx`/`stwcx`
   atomics are fine, so it is not an atomicity problem.
3. Decisive test: `volatile int a = 5, b = 7; if (a == b) __builtin_trap();`
   printed `trap test begin` but never `trap test survived` → the trap fires
   with a false condition.
4. `objdump` confirmed the codegen is correct at that site:
   `lwz r10,104(r1)` (5), `lwz r9,32(r1)` (7), `tweq r10,r9`.

**Workaround:** [`tools/wiiu/cemu-tweq-workaround.py`](../tools/wiiu/cemu-tweq-workaround.py)
NOPs every `tweq` in the executable sections of `devilutionx.elf`. It finds 9
of them. Run it **after the build and before `elf2rpl`**, and re-run it after
every rebuild (the ELF is regenerated).

The NOP'd guard is an unreachable overflow check (it could only fire after
2^31−1 copies of one control block), so patching is behaviour-neutral — the
patched binary is still correct on real hardware, and a single artifact can be
used for both. Drop the workaround once Cemu fixes `tweq`.

### 2. Lua needs `LUA_USE_C89` on PowerPC

Without it, Lua panics with `attempt to index a nil value` during
`LuaInitialize`. Fixed in `3rdParty/Lua/CMakeLists.txt` by adding `WIIU` to the
`LUA_USE_C89` platform list (next to 3DS, Vita, Switch, NXDK).

### 3. Input: it was a Cemu *settings* problem, not a code problem

The menu was unresponsive until the host keyboard was mapped to
**Controller 1 → GamePad** in Cemu's input settings (arrows → D-pad,
Enter/`4` → A). No `VPADInit`/`KPADInit` calls were needed — SDL's Wii U
backend handles that.

### 4. The system swkbd confirm button is broken on Cemu

`SDL_StartTextInput()` launches the Wii U system keyboard, whose ✓ button does
not close the dialog on Cemu 2.6 macOS. `Source/platform/wiiu/keyboard.cpp`
therefore skips swkbd entirely and just enables `SDL_TEXTINPUT`, letting the
normal `UiEdit` event loop (in `Source/DiabloUI/diabloui.cpp:228`) handle
Enter/Backspace like the PC version.

### 5. `elf2rpl` needs an unstripped ELF

`powerpc-eabi-strip` removes the `.rela.*` sections that `elf2rpl` needs, and
fails with `renameRplWrap failed`. The strip step is disabled by patching the
generated `build.make`. This is **lost on every `cmake` re-run**, so
`tools/wiiu/wiiu-run.sh` re-applies it automatically.

### 6. OSReport's printf does not support length modifiers

`%zu` and `%lu` print *nothing* **and swallow the trailing `\n`**, so two log
messages end up glued on one line. Always use `%d` / `%x` / `%s` with explicit
casts (e.g. `static_cast<int>(v.size())`).

### 7. Line endings: the repo uses `* -text` and many files are CRLF

`.gitattributes` sets `* -text`, so git never converts line endings — and a
number of upstream files (including `Source/engine/sound.cpp`) are committed
with **CRLF**. Editing those files with tools that normalize to LF produces a
whole-file diff. Convert back to CRLF before committing, or keep the editor
from rewriting the file.

### 8. SDL2 for Wii U has no filesystem backend

`libSDL2.a` for Wii U is built with the **dummy** filesystem backend. Verified by
disassembling the object file:

```bash
cd /tmp && mkdir sdl2x && cd sdl2x
/opt/devkitpro/devkitPPC/bin/powerpc-eabi-ar x /opt/devkitpro/portlibs/wiiu/lib/libSDL2.a
/opt/devkitpro/devkitPPC/bin/powerpc-eabi-objdump -dr SDL_sysfilesystem.o
```

Both functions are three instructions: `SDL_Error(SDL_UNSUPPORTED)` (code 4)
and `return NULL`. So on Wii U **`SDL_GetBasePath()` and `SDL_GetPrefPath()`
always return NULL**, and `Source/utils/paths.cpp` silently falls back to the
generic `#else` branch:

| Function | Value before the packaging fix |
|----------|-------------------------------|
| `BasePath()` | `""` |
| `AssetsPath()` | `"assets/"` |
| `PrefPath()` / `ConfigPath()` | `""` |

That is why the game used to work only when the CWD happened to be the SD card
root, and why it wrote saves and `diablo.ini` next to the game data. Nothing is
broken in SDL — the Wii U port simply never implemented these two calls, so the
port has to resolve its paths itself (`Source/platform/wiiu/init.cpp`).

## Build pipeline

```bash
cd build-wiiu-release

# 1. Build (strip must be disabled first — see finding 5)
make -j4

# 2. Cemu workaround — MUST run before elf2rpl (see finding 1)
python3 ../tools/wiiu/cemu-tweq-workaround.py devilutionx.elf

# 3. Convert + package as a self-contained Aroma-layout WUHB
../tools/wiiu/package-wuhb.sh        # elf2rpl + wuhbtool --content

# 4. Run
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g "$PWD/devilutionx.wuhb" -m /Users/mario/work/wiiu/cemu-mlc
```

All of this is wrapped by `tools/wiiu/wiiu-run.sh`.

### Build configuration

| Setting | Value | Notes |
|---------|-------|-------|
| Build dir | `build-wiiu-release` | `CMAKE_BUILD_TYPE=Release` |
| Toolchain | `/opt/devkitpro/cmake/WiiU.cmake` | devkitPro |
| `NONET` | `ON` | Network not implemented yet |
| `NOSOUND` | `OFF` | Sound enabled (removed from `CMake/platforms/wiiu.cmake`) |
| `BUILD_ASSETS_MPQ` | `OFF` | Assets are shipped loose, not baked into an MPQ |
| `DEVILUTIONX_SYSTEM_SDL2` | `ON` | SDL2 from devkitPro portlibs (2.32.10) |
| `DEVILUTIONX_SYSTEM_SDL_AUDIOLIB` | `OFF` | Bundled static SDL_audiolib is built from source |

> `NOSOUND`/`NONET` were passed as `-D…` on the original `cmake` command line,
> so they are stored in `CMakeCache.txt` as `UNINITIALIZED` and a platform-file
> change alone does not override them. Reconfigure with `-DNOSOUND=OFF` (or
> delete the cache) when changing these.

### Audio

The devkitPro SDL2 Wii U port ships a real AX audio driver (`SDL_wiiuaudio.c`,
`WIIUAUDIO_*`, `AXInitWithParams`/`AXAcquireVoice`), so no extra platform init
was needed. DevilutionX uses the bundled static SDL_audiolib
(`3rdParty/SDL_audiolib`). Confirmed at runtime:

```
Aulib::init done rate=22050 channels=2 frameSize=2048 format=9010
```

Cemu's AX emulation feeds the host via Cubeb.

### Artifacts

| File | Size | Purpose |
|------|------|---------|
| `build-wiiu-release/devilutionx.elf` | ~200 MB | Unstripped ELF, input to `elf2rpl` only |
| `build-wiiu-release/devilutionx.rpx` | ~3.9 MB | Wii U executable |
| `build-wiiu-release/devilutionx.wuhb` | ~41 MB | Self-contained WUHB: RPX + `assets/` in `/content/` + meta/icon |

`tools/wiiu/package-wuhb.sh` stages the assets into a temp dir as
`assets/…` and hands that to `wuhbtool --content`, so the WUHB ends up with
`/content/assets/…` and nothing else. It refuses to package if
`<assets>/spawn.mpq` is missing, because such a WUHB would boot to an error
dialog.

## Runtime layout

### Path resolution

`Source/platform/wiiu/init.cpp` resolves everything by hand, because SDL gives
us nothing on Wii U (see [finding 8](#8-sdl2-for-wii-u-has-no-filesystem-backend)).
`wiiu_init()` runs before `DiabloMain()` and does two things: log the
environment, then set the paths.

**Assets** — if `/vol/content/assets` exists (i.e. the WUHB was packaged with
`--content`), it is used; otherwise the historical CWD-relative layout is kept:

| Layout | `BasePath()` | `AssetsPath()` |
|--------|--------------|----------------|
| WUHB (`/vol/content/assets` exists) | `/vol/content/assets/` | `/vol/content/assets/` |
| Plain SD card (fallback) | `""` | `"assets/"` |

> `BasePath()` and `AssetsPath()` are the same directory in the WUHB layout
> because `spawn.mpq` lives inside `assets/`. The MPQ lookup
> (`GetMPQSearchPaths()` in `Source/engine/assets.cpp`) only searches
> `BasePath()`/`PrefPath()`/`ConfigPath()`/CWD, so the alternative would be
> duplicating the 24 MB MPQ at the content root.

**Writable data** — `/vol/external01/wiiu/apps/devilutionx/` is created on first
run (recursively, then verified with a real write+delete probe, because
`access()` is unreliable on FAT) and used for both `PrefPath()` and
`ConfigPath()`:

| What | Where |
|------|-------|
| `diablo.ini` | `<data dir>/diablo.ini` |
| Savegames | `<data dir>/save_*.sv` |
| Mods, demos, screenshots | `<data dir>/…` |

If the data dir cannot be created or written to, both paths fall back to `""`
(CWD).

`wiiu_log_paths()` is also called from `Source/diablo.cpp` after
`ApplicationInit()` and dumps the four resolved paths plus a probe of
`/vol/save`, so the log always shows which layout was picked.

### Layouts on disk

**WUHB (Aroma / Cemu, the shipped artifact):**

```
devilutionx.wuhb
└── /content/assets/…        read-only, mounted at /vol/content/assets

sd:/wiiu/apps/devilutionx/   writable, created on first run
├── diablo.ini
└── save_0.sv
```

**Plain SD card (dev builds, no content mount):**

```
sd:/
├── devilutionx_sd.wuhb (or .rpx via a loader)
├── assets/…                 ← legacy, still supported
└── wiiu/apps/devilutionx/   ← writes always land here now
```

### Emulated SD card

`~/Library/Application Support/Cemu/sdcard/` → `/vol/external01/`, CWD for the
guest is `/vol/external01`.

After packaging work the SD root holds **only** the data directory:

```
wiiu/apps/devilutionx/{diablo.ini, spawn_0.sv}
```

Older setups kept `assets/`, `spawn.mpq`, `diablo.ini` and `spawn_0.sv` at the
SD root; that still works as a fallback but is no longer needed.

Assets are packaged from `build-wiiu-clean/assets/` (a `BUILD_ASSETS_MPQ=ON`
build): `spawn.mpq`, `ASSETS_VERSION`, `fonts/`, `ui_art/`, `lua/`,
`lua_internal/`, `data/`, `txtdata/`, `gendata/`, `arena/`, `plrgfx/`, `*.gmo`.

### Source map

| File | Role |
|------|------|
| `Source/main.cpp` | `#ifdef __WIIU__` → `wiiu_init()` |
| `Source/platform/wiiu/init.cpp` | `wiiu_init()` — environment log + path setup |
| `Source/platform/wiiu/keyboard.cpp` | Text input, bypasses system swkbd |
| `Source/platform/wiiu/stub.cpp` | Empty placeholder — **unused, can be deleted** |
| `Source/platform/wiiu/CMakeLists.txt` | Platform object library |
| `Source/DiabloUI/diabloui.cpp` | Text-input dispatch to `wiiu_start_text_input` |
| `CMake/platforms/wiiu.cmake` | Platform build config |
| `3rdParty/Lua/CMakeLists.txt` | `WIIU` in the `LUA_USE_C89` list |
| `tools/wiiu/wiiu-run.sh` | Build → package → run → log helper |
| `tools/wiiu/package-wuhb.sh` | Packages the self-contained Aroma-layout WUHB |
| `tools/wiiu/cemu-tweq-workaround.py` | Cemu `tweq` workaround |
| `Packaging/wiiu/icon.png` | 128×128 WUHB icon (from `Packaging/resources/icon.png`) |

## Debug logging (OSReport)

The debug output is deliberately **kept in the tree** — it is the main debugging
tool for this port and will be needed again. It shows up in Cemu's log as
`[OSConsole]`.

```cpp
#ifdef __WIIU__
#include <coreinit/debug.h>
#define DBG(fmt, ...) OSReport("Tag: " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

DBG("value = %d", static_cast<int>(x));
```

Current instrumentation:

| File | Macro | Purpose |
|------|-------|---------|
| `Source/diablo.cpp` | `DBG` | `DiabloInit` step markers |
| `Source/lua/lua_global.cpp` | `DBG` | `LuaInitialize` step markers |
| `Source/platform/wiiu/init.cpp` | `DBG` | Environment dump + resolved paths |
| `Source/engine/sound.cpp` | `SND_DBG` | Audio load / `Aulib::init` tracing |
| `Source/utils/soundsample.cpp` | `SMPL_DBG` | `SoundSample::SetChunk` tracing |
| `Source/effects.cpp` | `SFX_DBG` | SFX load tracing (per-file — verbose, slows loading) |

`Source/engine/sound.cpp` additionally contains two throwaway test blocks (an
`std::atomic` test and a `__builtin_trap` test) that were used to prove the
`tweq` finding. They are dead weight now and are kept only because the port is
still being debugged actively. Remove them before any upstream PR.

> Cemu's `log.txt` is **rewritten** on every run, not appended — don't compare
> line counts between runs.

## Pitfalls

| Symptom | Cause / fix |
|---------|-------------|
| Guest hangs mid-load, log stops, Cemu alive at high CPU | Cemu `tweq` bug — run `tools/wiiu/cemu-tweq-workaround.py` before `elf2rpl` |
| `elf2rpl` → `renameRplWrap failed` | Strip must be disabled in `build.make` before converting |
| Lua panic `attempt to index a nil value` | `LUA_USE_C89` missing for WIIU in `3rdParty/Lua/CMakeLists.txt` |
| Menu does not react at all | Cemu input settings: map keyboard → Controller 1 (GamePad) |
| Text input ✓ button does nothing | swkbd broken on Cemu; `keyboard.cpp` bypasses it |
| Assets not found | Either `/vol/content/assets/spawn.mpq` (WUHB layout) or `./assets/spawn.mpq` (SD layout) must exist; see [Path resolution](#path-resolution) |
| Game writes saves/`diablo.ini` to the SD root | Running a WUHB that was packaged **without** `--content`, so the content mount is empty and the fallback layout is used |
| WUHB boots to "MPQ file(s) might be damaged" | Packaged without `--content`, or the assets dir had no `spawn.mpq` |
| `mkdir '/vol' failed (errno=5)` | Harmless — `/vol` is a virtual volume root; `EnsureDir()` ignores it |
| Two log lines glued together | OSReport printf: don't use `%zu` / `%lu` |
| Whole-file diff after editing | File is CRLF in git; don't let the editor rewrite endings |
| Black screen, no errors | Missing `assets/ASSETS_VERSION` (fails the version check) |

## Next steps

1. **Real hardware (Aroma) validation** (current task) — needs the user's Wii U;
   OSReport over UDP/serial.
2. **Clean up**: the throwaway `tweq` test blocks in `Source/engine/sound.cpp`, the
   per-file `SFX_DBG` spam, `Source/platform/wiiu/stub.cpp` (unused), the
   CRLF-only churn in the top-level `CMakeLists.txt`, and the now-redundant
   `-DLUA_USE_C89` in `CMake/platforms/wiiu.cmake`.
3. **Network** (`NONET=OFF` + `nsysnet` init, mirroring
   `Source/platform/switch/network.cpp`).
4. **Polish**: `DEVILUTIONX_GAMEPAD_TYPE Nintendo`, TV/DRC docking handling
   (Switch has `docking.cpp`), HOME/ProcUI integration.
5. **Investigate `/vol/save`** — on real hardware it is the per-title save volume
   and would be a better home for saves than the SD card. It does not exist on
   Cemu (already probed and logged as `exists=no` by `wiiu_log_paths()`), so this
   can only be settled on hardware.

## Session history

| Date | What happened |
|------|---------------|
| Oct 2026 | Port bootstrap: platform init, display, assets from SD, Lua fix, keyboard fix. Game reached the main menu. |
| Oct 2026 | Input "not working" traced to Cemu input settings, not code. Docs left stale. |
| Nov 2026 | Audio enabled (`NOSOUND=OFF`). Hang during the first sfx traced to Cemu's broken `tweq`; workaround written and validated. Sound confirmed working in-game. Docs rewritten to match reality; build/run tooling moved into `tools/wiiu/`. |
| Nov 2026 | Aroma-layout packaging. Found that SDL2 for Wii U has no filesystem backend (`SDL_GetBasePath()`/`SDL_GetPrefPath()` always `NULL`). `wiiu_init()` now resolves the paths: assets from the WUHB content mount (`/vol/content/assets`), writable data in `sd:/wiiu/apps/devilutionx/`. `package-wuhb.sh` produces a self-contained 41 MB WUHB; verified on Cemu with the SD card root emptied. |
