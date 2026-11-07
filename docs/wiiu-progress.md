# Wii U Port — Progress & Technical Reference

> **Role of this file:** technical reference + history. It answers *"how does this
> work and why is it this way"*. For *"where do I resume"*, see
> [`wiiu-handoff.md`](wiiu-handoff.md).

## Status (7 Nov 2026)

**Diablo (Shareware) is fully playable on Cemu 2.6.0 on macOS** — video, assets,
input, text input, audio, gameplay and savegames all work.

Not done yet: **Aroma-layout packaging** (next task), real-hardware test, network.

| Area | State | Notes |
|------|-------|-------|
| Boot / display | ✅ | SDL/GX2 renderer, main menu + in-game rendering |
| Assets | ✅ | Read from the emulated SD card root (`/vol/external01`) |
| Keyboard / mouse | ✅ | Works once mapped to Controller 1 (GamePad) in Cemu input settings |
| Text input | ✅ | `keyboard.cpp` bypasses the system swkbd |
| Audio (SFX + music) | ✅ | SDL2 Wii U **AX** audio driver + bundled SDL_audiolib |
| Savegames / config | ✅ | `spawn_0.sv`, `diablo.ini` written to the SD card root |
| Lua scripting | ✅ | Needs `LUA_USE_C89` on PowerPC |
| Network / multiplayer | ❌ | `NONET=ON`, no `nsysnet` init yet |
| Self-contained WUHB | ❌ | Assets currently live next to the WUHB on the SD card, not inside it |
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

## Build pipeline

```bash
cd build-wiiu-release

# 1. Build (strip must be disabled first — see finding 5)
make -j4

# 2. Cemu workaround — MUST run before elf2rpl (see finding 1)
python3 ../tools/wiiu/cemu-tweq-workaround.py devilutionx.elf

# 3. Convert + package
/opt/devkitpro/tools/bin/elf2rpl devilutionx.elf devilutionx.rpx
/opt/devkitpro/tools/bin/wuhbtool devilutionx.rpx devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# 4. Run
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g "$PWD/devilutionx_sd.wuhb" -m /Users/mario/work/wiiu/cemu-mlc
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
| `build-wiiu-release/devilutionx.rpx` | ~3.8 MB | Wii U executable |
| `build-wiiu-release/devilutionx_sd.wuhb` | ~3.8 MB | WUHB **without** assets — assets sit on the SD card |

## Runtime layout

### Path resolution

`Source/platform/wiiu/init.cpp` is currently a **no-op** (`void wiiu_init() {}`,
still called from `Source/main.cpp:42`). Nothing calls `paths::SetBasePath()` on
Wii U, so all paths come from SDL in `Source/utils/paths.cpp`:

| Function | Wii U value |
|----------|-------------|
| `BasePath()` | `SDL_GetBasePath()` |
| `AssetsPath()` | `BasePath() + "assets/"` (the generic `#else` branch) |
| `PrefPath()` / `ConfigPath()` | `SDL_GetPrefPath("diasurgical", "devilution")`, **but reset to `""`** if a writable `diablo.ini` exists in the CWD |

Observed behaviour is consistent with `BasePath() == "/vol/external01/"`: assets
are found at the SD root and `diablo.ini` / `spawn_0.sv` are written there.

> ⚠️ This is **inferred from behaviour, not logged**. Logging the four paths via
> OSReport is the first item of the packaging task.

### Emulated SD card

`~/Library/Application Support/Cemu/sdcard/` → `/vol/external01/`

```
assets/            # copied from a clean build's assets/ dir
spawn.mpq          # Shareware game data (must be at the SD root)
diablo.ini         # written by the game
spawn_0.sv         # savegame
devilutionx_sd.wuhb
```

Assets to copy (from `build-wiiu-clean/assets/`): `ASSETS_VERSION`, `fonts/`,
`ui_art/`, `lua/`, `lua_internal/`, `data/`, `txtdata/`, `gendata/`, `arena/`,
`plrgfx/`, `*.gmo` translations.

### Source map

| File | Role |
|------|------|
| `Source/main.cpp` | `#ifdef __WIIU__` → `wiiu_init()` |
| `Source/platform/wiiu/init.cpp` | `wiiu_init()` — currently empty |
| `Source/platform/wiiu/keyboard.cpp` | Text input, bypasses system swkbd |
| `Source/platform/wiiu/stub.cpp` | Empty placeholder — **unused, can be deleted** |
| `Source/platform/wiiu/CMakeLists.txt` | Platform object library |
| `Source/DiabloUI/diabloui.cpp` | Text-input dispatch to `wiiu_start_text_input` |
| `CMake/platforms/wiiu.cmake` | Platform build config |
| `3rdParty/Lua/CMakeLists.txt` | `WIIU` in the `LUA_USE_C89` list |
| `tools/wiiu/wiiu-run.sh` | Build → package → run → log helper |
| `tools/wiiu/cemu-tweq-workaround.py` | Cemu `tweq` workaround |

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
| Assets not found | Assets must be at the SD card root (`assets/`, `spawn.mpq`); `BasePath()` comes from SDL |
| Two log lines glued together | OSReport printf: don't use `%zu` / `%lu` |
| Whole-file diff after editing | File is CRLF in git; don't let the editor rewrite endings |
| Black screen, no errors | Missing `assets/ASSETS_VERSION` (fails the version check) |

## Next steps

1. **Aroma-layout packaging** (current task): self-contained WUHB with assets in
   `/content/`, no writes to the SD root, correct meta/icon — one artifact that
   behaves identically on Cemu and on Aroma. Starts by logging the four paths.
2. **Clean up** the throwaway `tweq` test blocks and trim the per-file `SFX_DBG`
   spam.
3. **Real hardware (Aroma) validation** — needs the user's Wii U; OSReport over
   UDP/serial.
4. **Network** (`NONET=OFF` + `nsysnet` init, mirroring
   `Source/platform/switch/network.cpp`).
5. **Polish**: `DEVILUTIONX_GAMEPAD_TYPE Nintendo`, TV/DRC docking handling
   (Switch has `docking.cpp`), HOME/ProcUI integration.

## Session history

| Date | What happened |
|------|---------------|
| Oct 2026 | Port bootstrap: platform init, display, assets from SD, Lua fix, keyboard fix. Game reached the main menu. |
| Oct 2026 | Input "not working" traced to Cemu input settings, not code. Docs left stale. |
| Nov 2026 | Audio enabled (`NOSOUND=OFF`). Hang during the first sfx traced to Cemu's broken `tweq`; workaround written and validated. Sound confirmed working in-game. Docs rewritten to match reality; build/run tooling moved into `tools/wiiu/`. |
