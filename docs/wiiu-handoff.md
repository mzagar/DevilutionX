# Wii U Port — Handoff

> **Role of this file:** where to resume. It answers *"what is the state, what do
> I do next, and how do I run it"*. For *how it works and why*, see
> [`wiiu-progress.md`](wiiu-progress.md).

## Where we are

Diablo (Shareware) **runs and is playable on a real Wii U under Aroma, and on
Cemu 2.6.0 on macOS, from the same self-contained Aroma-layout WUHB** — display,
assets, keyboard/mouse, text input, audio (SFX + music), gameplay and savegames
all work, and nothing is written to the SD card root.

**Real hardware was validated on 7 Nov 2026** (see below). Development stays
Cemu-first because that loop is much faster; the Wii U is for confirming
milestones.

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
- **Real hardware (Aroma)** — the same WUHB boots on a Wii U, mounts its content
  at `/vol/content`, resolves identical paths, and plays with audio + saves
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

## ✅ Real hardware (Aroma) — validated 7 Nov 2026

The packaged WUHB was copied to `sd:/wiiu/apps/devilutionx/` and booted on a real
Wii U under Aroma. **Everything works**: main menu, input, audio, gameplay, save
and load. The WUHB content is mounted at `/vol/content` exactly as on Cemu, and
all four resolved paths are identical.

From `boot-diag.txt` on the hardware boot:

```
content mount                '/vol/content' -> YES
content/assets/spawn.mpq     '/vol/content/assets/spawn.mpq' -> YES
save volume                  '/vol/save' -> YES
BasePath   = '/vol/content/assets/'
AssetsPath = '/vol/content/assets/'
PrefPath   = '/vol/external01/wiiu/apps/devilutionx/'
ConfigPath = '/vol/external01/wiiu/apps/devilutionx/'
```

The only two differences from Cemu, both cosmetic:

| | Cemu | Real Wii U |
|---|---|---|
| `/vol/save` | does not exist | exists + writable (Aroma redirects it to `sd:/wiiu/apps/save/B12DEFFB (DevilutionX)/`; **unused** — we write to the SD card) |
| `listdir '/vol'` | works | FAILED — harmless, diagnostic listing only |

So one artifact serves both, and the Cemu-first workflow stays trustworthy. Full
evidence: [`wiiu-progress.md`](wiiu-progress.md#9-real-hardware-aroma-verified-on-7-nov-2026).

## Boot diagnostics

`wiiu_init()` writes everything it reports to `boot-diag.txt` on the SD card
(`sd:/wiiu/apps/devilutionx/boot-diag.txt`, falling back to `sd:/boot-diag.txt`
if the data directory cannot be created). OSReport is invisible on real hardware
without extra tooling, so this file is the only way to debug a failed boot there.
It contains (example from a Cemu boot — see above for how the hardware values
differ):

```
=== DevilutionX Wii U boot diagnostics ===
build: Nov  7 2026 20:09:32 (init.cpp)   <- identifies which build is on the card
SDL: 2.32.10
=== Wii U environment ===
  cwd = 'fs:/vol/external01'
  SDL_GetBasePath = '(null)'             <- expected: SDL has no fs backend
  -- volumes --
  content mount                '/vol/content' -> YES
  content/assets/spawn.mpq     '/vol/content/assets/spawn.mpq' -> YES
  save volume                  '/vol/save' -> no
  -- listing /vol --
    [4] storage_mlc01
    [4] content
    [4] external01
assets: using WUHB content mount '/vol/content/assets'
pref/config: using '/vol/external01/wiiu/apps/devilutionx'
=== resolved devilution paths ===
  BasePath   = '/vol/content/assets/'
  ...
(this file: '/vol/external01/wiiu/apps/devilutionx/boot-diag.txt')
```

The resolved-paths section appears twice: once during `wiiu_init()` and once
re-checked after `ApplicationInit()`. The file is **overwritten on every boot**,
so read it before relaunching.

## 🎯 Next task: cleanup

**Goal:** make the branch presentable — before touching network or polish.

Nothing here changes behaviour; it is almost entirely removal.

1. `Source/engine/sound.cpp` — delete the two throwaway test blocks (an
   `std::atomic` test and a `__builtin_trap` test) left over from the `tweq`
   investigation. Keep the `SND_DBG` tracing.
2. `Source/effects.cpp` — trim the per-file `SFX_DBG` spam; it slows sound
   loading and floods the log.
3. `Source/platform/wiiu/stub.cpp` — delete it and its entry in
   `Source/platform/wiiu/CMakeLists.txt`.
4. Top-level `CMakeLists.txt` — revert the CRLF-only churn; the real change is
   just the `if(WIIU)` block.
5. `CMake/platforms/wiiu.cmake` — drop the redundant `-DLUA_USE_C89` (the fix
   lives in `3rdParty/Lua/CMakeLists.txt`) and the stale "Cemu signal-10 crash"
   comment block.

After that: **network** (`NONET=OFF` + `nsysnet`, mirroring
`Source/platform/switch/network.cpp`), then **polish**
(`DEVILUTIONX_GAMEPAD_TYPE Nintendo`, TV/DRC docking, HOME/ProcUI).

## ℹ️ How the packaging works

```
devilutionx.wuhb
└── /content/assets/…        read-only, mounted at /vol/content/assets

sd:/wiiu/apps/devilutionx/   writable, created on first run
├── diablo.ini
├── save_0.sv
└── boot-diag.txt            <- rewritten on every boot, see above
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
| `Source/platform/wiiu/init.cpp` | `wiiu_init()` — environment log + path setup + `boot-diag.txt` |
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
| Data on SD | `sd:/wiiu/apps/devilutionx/` (`diablo.ini`, `save_*.sv`, `boot-diag.txt`) |
| Real SD card (tested) | `sd:/wiiu/apps/devilutionx/devilutionx.wuhb` — same layout as Cemu, nothing else needed |

## Notes for whoever continues

- **Keep the debug logging** — it is the primary debugging tool for this port.
- `Source/engine/sound.cpp` still has two throwaway test blocks (an atomic test
  and a `__builtin_trap` test) from the `tweq` investigation. Harmless, but
  remove them before an upstream PR.
- OSReport's printf does **not** support `%zu`/`%lu` — use `%d` with casts.
- Many repo files are CRLF in git (`.gitattributes` has `* -text`) — don't let
  an editor rewrite line endings, or you get a whole-file diff.
