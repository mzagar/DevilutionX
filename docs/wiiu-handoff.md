# Wii U Port — Handoff

> **Role of this file:** where to resume. It answers *"what is the state, what do
> I do next, and how do I run it"*. For *how it works and why*, see
> [`wiiu-progress.md`](wiiu-progress.md).

## Where we are

Diablo **runs and is playable on a real Wii U under Aroma, and on Cemu 2.6.0 on
macOS, from the same self-contained Aroma-layout WUHB** — display, assets,
keyboard/mouse, text input, audio (SFX + music), gameplay and savegames all
work, and nothing is written to the SD card root.

**Shareware, the full game and the Hellfire expansion all work**, and the
artifact is **fully self-contained**: engine, shareware fallback and the Hellfire
mod are inside the WUHB. The SD card carries only the retail MPQs (the user's own
game data) and whatever the game writes — see
[Full game + Hellfire](#-full-game--hellfire) below.

**Real hardware was validated on 7 and 8 Nov 2026** (see below). Development
stays Cemu-first because that loop is much faster; the Wii U is for confirming
milestones. Everything on the Wii U so far has matched Cemu exactly, so the
Cemu loop is trustworthy.

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
- **Full game** — `DIABDAT.MPQ` is picked up from the SD data directory, giving
  `gbIsSpawn=0` and the intro cinematics
- **Hellfire** — the expansion is a Lua **mod**; it is packed into `hf.mpq` and
  shipped *inside* the WUHB, so no `mods/` directory is needed on the SD card.
  Hellfire gets its own `.hsv` save namespace
- Build/package pipeline (including the mod packing) + the Cemu `tweq` workaround,
  all scripted in `tools/wiiu/`

## ⚠️ Three things that will bite you

1. **The Cemu `tweq` bug.** Cemu 2.6.0 takes `tweq` traps even when the
   condition is false, and GCC emits `tweq` for libstdc++'s shared_ptr
   use-count guard — so **any shared_ptr copy hangs the guest**. The symptom is
   a silent hang: the log stops mid-line and Cemu sits at ~200 % CPU. Always run
   `tools/wiiu/cemu-tweq-workaround.py` on `devilutionx.elf` **before**
   `elf2rpl`, and re-run it after every rebuild. Full story:
   [`wiiu-progress.md`](wiiu-progress.md#1-cemus-tweq-traps-even-when-the-condition-is-false-the-big-one).

2. **`elf2rpl` needs an unstripped ELF.** The strip patch is lost on every
   `cmake` re-run; `wiiu-run.sh` re-applies it.

3. **Hellfire is a Lua *mod*, and its loader is silent.** `gbIsHellfire` is set
   only by `mods/hf/init.lua`, and `RunScript(..., optional=true)` reports nothing
   when a mod is missing. The symptom is deceptive: the MPQs are present,
   `HaveHellfire=1`, the *Enter Hellfire* dialog appears — and the class list
   still has just three classes, no Monk. Check `assets/mods/hf.mpq` is inside
   the WUHB. See [Full game + Hellfire](#-full-game--hellfire).

## ✅ Real hardware (Aroma) — validated 7–8 Nov 2026

The packaged WUHB was copied to `sd:/wiiu/apps/devilutionx/` and booted on a real
Wii U under Aroma. **Everything works**: main menu, input, audio, gameplay, save
and load. The WUHB content is mounted at `/vol/content` exactly as on Cemu, and
all four resolved paths are identical.

On 8 Nov 2026 the **final self-contained artifact** was validated the same way:
engine + shareware assets + `mods/hf.mpq` in one WUHB, SD card carrying only the
retail MPQs and the game's own files. Hellfire plays identically to Cemu, which
is what makes the Cemu-first loop trustworthy — the Wii U is only needed to
confirm milestones.

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

## 🎮 Full game + Hellfire

The shareware `spawn.mpq` is packaged inside the WUHB so the port boots out of
the box. The **full game works too** — verified on Cemu on 7 Nov 2026 with
`gbIsSpawn=0 HaveIntro=1 HaveHellfire=1`.

Copy these next to the WUHB on the SD card (`sd:/wiiu/apps/devilutionx/`):

| File | Size | What it is |
|------|------|------------|
| `DIABDAT.MPQ` | 493 MiB | base game data |
| `hellfire.mpq` | 62 MiB | Hellfire data |
| `hfmonk.mpq` | 36 MiB | Hellfire Monk class |
| `hfmusic.mpq` | 33 MiB | Hellfire music |
| `hfvoice.mpq` | 36 MiB | Hellfire voice lines |

`Patch_rt.mpq` and `DRTL_IX86_108_109.mpq` are **not** needed — DevilutionX has
its own fixes and never reads them. `hfbard.mpq`/`hfbarb.mpq` (Bard/Barbarian
classes) are optional extras the GOG installer does not ship; without them
`HaveBardAssets`/`HaveBarbarianAssets` are simply false.

Extract them from the GOG "Diablo + Hellfire" installer:

```bash
tools/wiiu/extract-gog-mpqs.sh \
  ~/Downloads/setup_diablo_1.09_hellfire_v4_\(78466\).exe ./gog-mpq
```

The GOG installer is an **Inno Setup** archive, so this needs `innoextract`. The
script falls back to running it in a throwaway Docker container. Prefer that:
Homebrew has no bottle for this formula's `boost` dependency, so `brew install
innoextract` builds boost from source and takes the better part of an hour.

Then copy them across:

```bash
DEST=~/Library/Application\ Support/Cemu/sdcard/wiiu/apps/devilutionx   # Cemu
# DEST=/Volumes/<SD>/wiiu/apps/devilutionx                              # real card
cp gog-mpq/*.mpq "$DEST/"
```

### Diablo vs Hellfire

`DIABDAT.MPQ` alone gives the full (non-shareware) Diablo. The Hellfire MPQs add
the expansion. Which one you play is the `Game` key in `diablo.ini`:

| `[GameMode] Game` | Meaning |
|---|---|
| `0` | Ask — shows an *Enter Hellfire* / *Switch to Diablo* dialog at startup when the Hellfire MPQs are present |
| `1` | Hellfire |
| `2` | Diablo |

The option is `OptionEntryFlags::Invisible` in `Source/options.cpp`, so it is
**not** in the settings menu — edit `diablo.ini`, or start once with `Game=0`
and use the dialog. A fresh card starts at `0`.

#### `Game` is only the prompt — the real switch is the `hf` mod

The expansion itself is a Lua **mod**, and `gbIsHellfire` is set only by
`mods/hf/init.lua` calling `hellfire.enable()`. Having the MPQs is **not enough**:
if the mod is missing, the script loader
(`RunScript(..., /*optional=*/true)`) fails **silently** — no log line anywhere —
and you get the *Enter Hellfire* dialog, a normal-looking game, and **only three
classes, no Monk, no Hive or Crypt**. That was the bug that took longest to find.

So: `[Mods] hf=1` in `diablo.ini` must be set **and** the mod must be findable.
The port ships it inside the WUHB as `assets/mods/hf.mpq`, so the SD card needs
nothing. If you ever rebuild the mod:

```bash
tools/wiiu/build-hf-mpq.sh      # mods/hf/  ->  build-wiiu-release/mods/hf.mpq
tools/wiiu/package-wuhb.sh      # stages it into the WUHB
```

It has to be a **packed MPQ**, not a loose directory: only packed mods register a
mod identifier, which is what makes the manifest's `saveExtension=hsv` take
effect. As a loose directory Hellfire silently falls back to the Diablo save
namespace. Full explanation:
[`wiiu-progress.md`](wiiu-progress.md#11-the-hellfire-expansion-is-a-lua-mod-not-game-data).

### Saves: Diablo and Hellfire are separate

Save files are `<prefix><n>.<ext>`, and both parts matter:

| | Prefix | Extension |
|---|---|---|
| shareware | `spawn_` / `share_` | `sv` |
| full game, Diablo | `single_` / `multi_` | `sv` |
| full game, Hellfire | `single_` / `multi_` | **`hsv`** |

Consequences worth knowing:

- A character made in **shareware** does not appear in the full game. Renaming
  `spawn_1.sv` → `single_1.sv` brings it across.
- A **Diablo** character does not appear in **Hellfire**, and copying the file
  across does **not** work — the names *inside* the save archive are numbered by
  the dungeon-level count (17 vs 25), so the copy is rewritten and then fails with
  *"Unable to open save file archive"*. **Play Hellfire with a fresh
  character.** Details:
  [`wiiu-progress.md`](wiiu-progress.md#12-diablo-and-hellfire-saves-are-not-interchangeable).

### Why the MPQs go next to the WUHB

`GetMPQSearchPaths()` (`Source/engine/assets.cpp:441`) searches BasePath →
PrefPath → ConfigPath → CWD. On Wii U that is `/vol/content/assets/` (read-only,
inside the WUHB), `/vol/external01/wiiu/apps/devilutionx/`, and the SD root — so
the data directory is the only sensible place. `DIABDAT.MPQ` is tried before
`spawn.mpq`, so it takes priority automatically and the bundled shareware stays
as a fallback.

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

## 🎯 Next task: undecided — brainstorm, then this cleanup

The branch has reached a good baseline: everything works on both Cemu and real
hardware, and the artifact is self-contained. **The next feature is still to be
chosen.** Candidates on the table:

- **Local multiplayer on one Wii U** — 2–4 players with their own controllers.
  Three shapes were proposed: (a) split-screen, Mario Kart style; (b) everyone on
  one shared screen, so nobody can wander off alone; (c) a hybrid that stays
  shared while the party is together and splits when a player walks away.
- **Network multiplayer** — `NONET=OFF` + `nsysnet`, mirroring
  `Source/platform/switch/network.cpp`.
- **Polish** — `DEVILUTIONX_GAMEPAD_TYPE Nintendo`, TV/DRC docking, HOME/ProcUI.

Whenever that is settled, this cleanup comes first (it makes the feature diff
easier to read, and the debug output it removes is not needed for the feature):

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
└── /content/assets/…           read-only, mounted at /vol/content/assets
    ├── spawn.mpq               shareware fallback (fonts/, data/, ui_art/…)
    └── mods/hf.mpq             the Hellfire mod, packed

sd:/wiiu/apps/devilutionx/      writable, created on first run
├── devilutionx.wuhb            on the real card (launched from here)
├── DIABDAT.MPQ                 retail base game data
├── hellfire.mpq + hf*.mpq      retail Hellfire data
├── diablo.ini
├── single_0.sv                 one per character (Diablo)
├── single_0.hsv                Hellfire characters, separate namespace
├── stash.sv
└── boot-diag.txt               <- rewritten on every boot, see above
```

There is **no `mods/` directory on the SD card** — the mod lives in the WUHB.
`diablo.ini`, the saves and `boot-diag.txt` are the only files the game creates.

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
| `tools/wiiu/package-wuhb.sh` | Packages the self-contained Aroma-layout WUHB (assets + `mods/hf.mpq`) |
| `tools/wiiu/build-hf-mpq.sh` | Packs `mods/hf/` into `hf.mpq` (needs `smpq`; uses Docker if missing) |
| `tools/wiiu/cemu-tweq-workaround.py` | Cemu `tweq` workaround |
| `tools/wiiu/extract-gog-mpqs.sh` | Pulls the retail MPQs out of the GOG installer |
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
| GOG installer | `~/Downloads/setup_diablo_1.09_hellfire_v4_(78466).exe` (850 MB, Inno Setup 5.6.2) |
| Extracted retail MPQs | `gog-mpq/` in the repo root (gitignored) |

## Notes for whoever continues

- **Keep the debug logging** — it is the primary debugging tool for this port,
  and the next feature (multiplayer) will need it more, not less. That includes
  the two throwaway `tweq` test blocks in `Source/engine/sound.cpp` and the
  per-file `SFX_DBG` spam in `Source/effects.cpp`, which are listed under cleanup
  but deliberately **not** removed yet.
- **Hellfire is a mod.** If Hellfire ever silently stops working (three classes,
  no Monk), check that `assets/mods/hf.mpq` is inside the WUHB. The mod loader is
  `optional=true`, so it never reports the failure.
- **Never copy saves between `.sv` and `.hsv`** — the archive's internal names are
  numbered by dungeon-level count, so the file gets rewritten and then fails to
  load. See [Saves](#saves-diablo-and-hellfire-are-separate).
- `Source/engine/sound.cpp` still has two throwaway test blocks (an atomic test
  and a `__builtin_trap` test) from the `tweq` investigation. Harmless, but
  remove them before an upstream PR.
- OSReport's printf does **not** support `%zu`/`%lu` — use `%d` with casts.
- Many repo files are CRLF in git (`.gitattributes` has `* -text`) — don't let
  an editor rewrite line endings, or you get a whole-file diff.
