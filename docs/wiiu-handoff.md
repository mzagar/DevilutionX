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

**Network multiplayer (TCP) works with the Wii U as the joining peer**: a real
dvlnet session against a desktop host was validated in Cemu — join, game data,
broadcast/unicast messages, turns, passwords and leave reasons all pass, using
the same engine code as every other platform. The Wii U as *host* is blocked by
a Cemu self-connect crash (not our code) and needs a real console to confirm.
The machine-readable test harness and the exact commands are in
[Next task](#-next-task-finish-network-multiplayer-parity).

## ⚡ Quick start

```bash
cd path/to/DevilutionX
tools/wiiu/wiiu-run.sh 40              # configure → build → patch → package → run → log
tools/wiiu/wiiu-run.sh 40 --no-build   # re-run without rebuilding
```

### From a fresh clone

Prerequisites:

- devkitPro with the Wii U toolchain: `devkitPPC`, `wut`, `wiiu-cmake`,
  `wiiu-sdl2`, `wiiu-pkg-config`, and `wut-tools` (`elf2rpl`, `wuhbtool`).
- CMake 3.22 or newer, and `make` (or Ninja).
- Optional: Docker, for `tools/wiiu/build-hf-mpq.sh` when `smpq` is missing.

```bash
cmake -S . -B build-wiiu-release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=Release

tools/wiiu/build-hf-mpq.sh        # pack mods/hf -> build-wiiu-release/mods/hf.mpq
tools/wiiu/wiiu-run.sh 40         # build → unstrip → tweq → package → Cemu

# The WUHB is at build-wiiu-release/devilutionx.wuhb. Copy it to the SD card
# (sd:/wiiu/apps/devilutionx/) together with the retail MPQs.
```

`package-wuhb.sh` downloads the shareware `spawn.mpq` once if it is missing.
The first `wiiu-run.sh` run configures the build directory. Set `WIIU_MLC` if
Cemu does not have its MLC path configured.

Log:

```bash
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

## ✅ Done

- Display / SDL-GX2 renderer, main menu and in-game rendering
- Keyboard/mouse — works once mapped to Controller 1 (GamePad) in Cemu input settings
- Text input — the game's own `UiEdit` + a USB keyboard (`nsyskbd`); Cemu has neither
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
- **Network stack (asio + nsysnet + libsodium)** — `NONET=OFF`; asio TCP
  `resolve`/`connect`/`write`/`read` verified end-to-end under Cemu, which
  bridges `nsysnet` to the host network
- **Network multiplayer (TCP), no-UI harness** — `test/network_peer.cpp` +
  `tools/wiiu/package-network-peer.sh`: a real Wii U ↔ desktop dvlnet session
  (join, messages, turns, private games, leave reasons) with JSONL results,
  no menus or input needed. See
  [Next task](#-next-task-finish-network-multiplayer-parity)
- **Secure RNG for libsodium** — the console has no OS RNG, so
  `Source/platform/wiiu/random.cpp` installs a ChaCha20 DRBG (or `/dev/urandom`
  when available) before `sodium_init()`; previously the first join aborted
  with `OSFatal`
- Build/package pipeline (including the mod packing) + the Cemu `tweq` workaround,
  all scripted in `tools/wiiu/`

## ⚠️ Four things that will bite you

1. **The Cemu `tweq` bug.** Cemu 2.6.0 takes `tweq` traps even when the
   condition is false, and GCC emits `tweq` for libstdc++'s shared_ptr
   use-count guard — so **any shared_ptr copy hangs the guest**. The symptom is
   a silent hang: the log stops mid-line and Cemu sits at ~200 % CPU. Always run
   `tools/wiiu/cemu-tweq-workaround.py` on `devilutionx.elf` **before**
   `elf2rpl`, and re-run it after every rebuild. Full story:
   [`wiiu-progress.md`](wiiu-progress.md#1-cemus-tweq-traps-even-when-the-condition-is-false-the-big-one).

2. **`elf2rpl` needs an unstripped ELF.** The strip patch is lost on every
   `cmake` re-run; `wiiu-run.sh` re-applies it.

3. **`pthread_key_create` always fails on Wii U (`ENOSYS`).** asio's
   `posix_tss_ptr` throws during *static initialisation*, before any UI exists —
   and `ErrDlg` crashes there too, so the failure is completely silent (the only
   clue is `__do_global_ctors_aux` in a stack dump). Fixed with
   `ASIO_DISABLE_THREADS=ON`, same as 3DS. Other asio landmines on this port: no
   `pipe()` (shimmed over a TCP loopback pair), no `<termios.h>`
   (`ASIO_DISABLE_SERIAL_PORT`), and the umbrella `<asio.hpp>` **cannot** be used
   (its `asio::local` conflicts with wut's `sockaddr`) — include specific headers.

4. **Hellfire is a Lua *mod*, and its loader is silent.** `gbIsHellfire` is set
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

## 🧩 Local co-op: playable (shared camera)

Branch `wiiu-splitscreen`. **Two local players already play in one world on Cemu**
with a shared camera. Split screen is the next milestone and is not implemented yet.

Status:

- `InitLocalCoop(seats)` starts a no-UI local session on the loopback provider:
  one hero save per seat, seats placed on the town spawn table, single-player
  rules (no turn worker), `gbActivePlayers = seats`.
- Seat-owned commands: every logical packet is delivered once with the acting
  seat as sender (`SNetSendMessageAs`), and the packet header describes the acting
  player. Without the header fix seat 1 teleported to seat 0.
- Input ownership: `InputSeats` (device instance ID -> seat, with generation) and
  `SeatInput` (per-seat buttons and stick axes). Seat 0's legacy motion state is
  only fed by seat 0's pad; `plrctrls_after_game_logic()` drives seats 1..N
  through the production walk/attack path with the sender tag set per seat.
- Console entry — `sd:/wiiu/apps/devilutionx/local_coop.ini`:

  ```ini
  [LocalCoop]
  Seats=2
  AutoStart=1   ; dev switch: skip the menu and start hero 0
  ```

  Seat N loads `single_N.sv` (or `spawn_N.sv` in shareware). Missing or invalid
  values keep ordinary single-player behaviour; malformed values never abort.
- `local_session_runner --seats N` proves N-seat sessions and per-seat movement
  without UI, on host and on the console, via JSONL checkpoints per seat.

Cemu check (verified 16 Nov 2026):

```bash
tools/wiiu/cemu-run.sh build-wiiu-release/devilutionx.wuhb
```

GamePad = seat 0. Add a **Wii U Pro Controller** as Cemu Player 2 -> seat 1 (hot-plug
works). Log confirmation:

```
Local co-op: 2 seats (autostart=true)
Local co-op session with 2 seats
Adding joystick 1: WiiU Pro Controller #1
```

Four seats boot too (`Seats=4`, `single_0..3.sv`); input with four pads is untested.

Split screen (first pass): with `SplitScreen=1` in `local_coop.ini`, two seats get
independent cameras composited side by side; the main panel stays seat 0's.
Each side is the central half of a normal 640x352 view (cropped, not rescaled),
so the field of view is narrower; per-seat panels/compact HUD are next.
Verified in Cemu on 17 Nov 2026: two different town views in one frame.

Known gaps at this milestone:

- Two-seat split only (`Seats=4` falls back to the shared camera); no independent
  travel.
- Seat 2 only walks and melee-attacks; pickups, doors, interact, spells, belt and
  the panels are still seat 0's.
- The HUD belongs to the camera owner.

Next: split-screen rendering (two cameras -> two logical views -> per-view palette
-> one composite), then compact per-seat HUD, then four cells. The full plan is in
[`wiiu-local-split-screen-implementation-handoff.md`](wiiu-local-split-screen-implementation-handoff.md).

### Reproduce the runner gate (still useful)

```bash
tools/wiiu/package-local-session.sh
tools/wiiu/cemu-run.sh build-wiiu-net/local_session_runner.wuhb
cat "$HOME/Library/Application Support/Cemu/sdcard/wiiu/apps/devilutionx/runner/local_session_result.jsonl"
```

Host-only, two seats, no Cemu:

```bash
cmake -S . -B build-split -DBUILD_TESTING=ON -DBUILD_LOCAL_SESSION_RUNNER=ON
ninja -C build-split local_session_runner
./build-split/local_session_runner --data-dir build-split/assets \
  --pref-dir /tmp/lsr --create-hero warrior --seats 2 --ticks 20 --walk 4
```

## 🎯 Next task: finish network multiplayer parity

**The goal is parity: the Wii U should do what every other platform already does**,
before anything new is invented. Local co-op is now its own track on branch
`wiiu-splitscreen` (see above); the shared-camera build already plays with two
local players, and split screen is the next milestone.

### Where it stands

| Milestone | State |
|---|---|
| **M0** feasibility probe | ✅ Cemu's `nsysnet` bridges to the host network |
| **M1** socket init | ✅ `wiiu_enable_network()` from `wiiu_init()` |
| **M2** asio shims | ✅ 13 files under `Source/platform/wiiu/asio/` |
| **M3** encryption (`libsodium`) | ✅ bundled, builds, **and has a working RNG** (finding 20) |
| **M3.5** dvlnet reachability | ✅ boot probe + raw socket probe (`probe = socket` in the harness) |
| **M4a** real dvlnet session, Wii U as client | ✅ **validated in Cemu** — harness (join, game data, messages, turns, passwords, `LEAVE_EXIT`) **and the real game** (macOS host + Wii U joiner, both playable) |
| **M4b** real dvlnet session, Wii U as host | ⬜ blocked in Cemu (guest self-connect crashes Cemu); needs real hardware |
| **M5** address-entry UX + multiplayer UI | ⬜ not started |
| **M6** real hardware multiplayer | ⬜ needs the M4b run on a console |
| **ZT** ZeroTier provider | ⬜ separate milestone — **not in the build** (`DISABLE_ZERO_TIER ON`); the connection menu shows TCP + Loopback only |

Shared protocol fixes that came out of this work (32-bit BE vs 64-bit LE, the
asio write serialisation bug, the custom-port self-connect) are listed as
findings 16–20 in [`wiiu-progress.md`](wiiu-progress.md).

### ZeroTier is not in this build

`CMake/platforms/wiiu.cmake` sets `DISABLE_ZERO_TIER ON` (the same as the
Switch profile), and `SelconnLoad()` only adds the ZeroTier entry when that
macro is undefined. That is why the connection menu lists **Client-Server
(TCP)** and **Loopback** only — it is a compile-time exclusion, not a Cemu
problem.

The reason is size, not policy. DevilutionX does not use a system ZeroTier
client: it embeds **libzt**, which brings its own user-space TCP/IP stack
(lwIP, NAT-PMP, UPnP) plus the node identity and relay logic. The TCP provider
reuses the existing asio + `nsysnet` sockets, so it was the parity-first
choice. A Wii U provider would need libzt ported to devkitPPC/wut
(`zerotier_lwip` vs `zerotier_native` is the open feasibility question).

ZeroTier still works at the network level today:

- Run ZeroTier One on the host OS (macOS for Cemu, or a router/bridge for real
  hardware).
- Host the game over TCP and let remote players reach the machine's ZeroTier
  address; on Cemu, `nsysnet` bridges the guest sockets to the host stack, so
  the join dialog accepts a `10.x.y.z` address.
- Port 6112 must be reachable on the ZeroTier interface.

The only thing lost is game discovery: the TCP provider has no gamelist, so the
address must be entered (or taken from `[Network] Previous Host`).

#### Setup for two households

This is the whole recipe for a game between two homes. The free ZeroTier plan
is enough for a handful of players.

1. **Create the network.** Sign in at <https://my.zerotier.com>, create a
   network and copy its **Network ID** (16 hex characters). Leave the access
   control on "private" and authorise devices as they appear.
2. **Join both machines.** Install ZeroTier One on the host machine and on the
   other player's machine:

   ```bash
   sudo zerotier-cli join <NetworkID>
   zerotier-cli listnetworks     # wait for OK and note the 10.147.17.x address
   ```

   In the web UI, tick both devices. Each one gets a managed `10.147.17.x`
   address.
3. **Pick the game mode.**
   - *Desktop build with the ZeroTier provider:* Multiplayer → ZeroTier →
     enter the network name/ID. The game list then shows hosted games by name.
   - *Wii U (or any build without the provider):* host over **TCP** on the
     machine that runs ZeroTier, and join the host's `10.147.17.x:6112`.
4. **Cemu:** nothing else is needed. `nsysnet` bridges the guest sockets to the
   host network stack, so the join dialog accepts the `10.147.17.x` address.
5. **Real Wii U:** the console has no ZeroTier client. Run ZeroTier on the LAN
   gateway (a router that supports it, or a PC/Mac that routes
   `10.147.17.0/24`), then enter the host's `10.147.17.x` on the console.
   Without that route the console cannot reach the virtual network.

Quick checks before starting a game:

```bash
ping -c 3 10.147.17.x              # virtual network reachable?
nc -vz 10.147.17.x 6112            # host listener reachable?
zerotier-cli peers                 # DIRECT (not RELAY) is better for latency
```

Notes:

- No port forwarding is needed; do not expose 6112 on the router.
- TCP has no discovery, so the address must be typed or come from
  `[Network] Previous Host`.
- If `zerotier-cli peers` shows `RELAY`, the game still works but adds latency;
  direct paths usually establish after a few seconds.
- The free plan has a device limit that changes over time; check the current
  terms before inviting a larger group.

### How to run a real session without the UI

The harness drives the same SNet/dvlnet code as the game but skips menus,
archives and heroes, so it runs on the console without input.

Desktop build:

```bash
cmake -S . -B build-net -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DBUILD_NETWORK_HARNESS=ON -DNONET=OFF -DDISABLE_TCP=OFF -DDISABLE_ZERO_TIER=ON \
  -DPACKET_ENCRYPTION=ON -DDEVILUTIONX_SYSTEM_GOOGLETEST=OFF -DDEVILUTIONX_SYSTEM_BENCHMARK=OFF
cmake --build build-net --target network_peer dvlnet_wire_test

# Desktop ↔ desktop matrix (public, private, 4 players, custom port, drop,
# wrong password) plus the codec/round-trip tests:
./build-net/dvlnet_wire_test
python3 test/run_network_scenario.py --peer build-net/network_peer --players 4 --rounds 512
```

Console build + package:

```bash
cmake -S . -B build-wiiu-net -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_NETWORK_HARNESS=ON -DBUILD_ASSETS_MPQ=OFF
make -C build-wiiu-net network_peer -j8
tools/wiiu/package-network-peer.sh       # elf2rpl + wuhbtool -> build-wiiu-net/network_peer.wuhb
```

Then put `network_peer.ini` on the SD card
(`sd:/wiiu/apps/devilutionx/network_peer.ini`) and launch the WUHB in Cemu:

```ini
role = join          # or host
players = 2
port = 6112
connect = 127.0.0.1:6112
password = secret    # omit or "-" for a public game
rounds = 256
scenario = wiiu-tcp
deadline = 120
```

Results are JSONL on stdout/OSReport and in
`sd:/wiiu/apps/devilutionx/network_peer_result.jsonl`. **Cemu needs SIGPIPE
ignored** or it dies with signal 13 as soon as the guest writes to its socket:

```bash
python3 -c 'import os,signal,sys; signal.signal(signal.SIGPIPE, signal.SIG_IGN); os.execv("/Applications/Cemu.app/Contents/MacOS/Cemu", sys.argv[:1]+sys.argv[1:])' \
  -g build-wiiu-net/network_peer.wuhb      # add "-m <MLC>" only if Cemu is not configured
```

### Real game, manual UI (already confirmed working)

Host on the Mac, join from Cemu:

```bash
cd path/to/DevilutionX

open -n build-macos/devilutionx.app            # Multiplayer → TCP → Host Game
tools/wiiu/cemu-run.sh build-wiiu-release/devilutionx.wuhb
```

In Cemu: Multiplayer → Client-Server (TCP) → Join Game → `127.0.0.1` is
pre-filled → empty password → OK → pick the multiplayer hero. Both sides must
be in the same game mode; the working setup is Diablo (full game) with
`[GameMode] Game=2` and `[Mods] hf=0` on the console, and `Game=2` on the Mac.
The hero archive `multi_0.sv` can be copied between the two: saves are packed
little-endian.

`tools/wiiu/cemu-run.sh` ignores SIGPIPE — Cemu 2.6.0 otherwise dies with
signal 13 on the first multiplayer packet.

### Cemu-only facts (not our code)

1. **Cemu has no keyboard.** `nsyskbd.KBDSetup` is an unsupported lib call and
   `nsyshid` emulates no USB keyboard, so SDL never gets a key event. On real
   hardware a USB keyboard types straight into the game's own `UiEdit` (the PC
   path in `keyboard.cpp`). The system swkbd is no alternative: Cemu's `swkbd`
   HLE never hides on confirm and never clears its "OK pressed" flag, so SDL
   re-sends the same string every frame (measured: 6931 events in ~15 s). See
   finding 15 in `wiiu-progress.md`.
2. **Wii U as host crashes Cemu on the self-connect.** The host path binds and
   listens (verified by the raw-socket probe), then `tcp_client::create()`
   connects to its own listener. In Cemu that is immediately followed by a
   `SIGBUS` inside Cemu (guest stack: `tcp_client::poll` → `join` → `create`),
   the same self-connect failure the earlier investigation hit. It is a Cemu
   bug: raw `socket`/`bind`/`listen`/`connect`/`accept` all work on the guest.
   Needs a real Wii U to validate hosting.
3. **`getsockname()` lies in Cemu.** It reports success with an out-of-range
   `socklen`, which makes asio's `endpoint::resize()` throw. Never call
   `local_endpoint()` on the acceptor without a fallback (already handled in
   `tcp_server::LocalhostSelf()`).
4. **A stale Cemu holds `*:6112`.** `bind(0.0.0.0:6112)` then fails with
   Cemu's own code `109999`. Kill leftover Cemu processes before hosting.

### Driving Cemu's UI from a script does not work

Synthetic events (`osascript ... key code`, System Events clicks) reach the
native macOS build but **not** Cemu's guest, even with the GamePad keyboard
mappings restored and `Pause Game When Window Loses Focus=0`. That is why the
harness above exists; the real UI still needs a human (or a real keyboard on
hardware).

### State of the tree (uncommitted)

- `Source/dvlnet/spawned_monster_delta.hpp` — new ABI-independent wire codec.
- `Source/msg.cpp`, `Source/pack.cpp`, `Source/sync.cpp`,
  `Source/dvlnet/base.cpp` — the BE/LE fixes from the shared-blockers list.
- `Source/dvlnet/tcp_server.{h,cpp}`, `Source/dvlnet/tcp_client.{h,cpp}` —
  per-socket write queues, non-throwing acceptor setup, port-aware
  `LocalhostSelf()`, graceful-leave flush.
- `Source/platform/wiiu/random.{hpp,cpp}` — libsodium RNG backend; wired into
  `Source/main.cpp`, `Source/platform/wiiu/CMakeLists.txt` and `boot-diag.txt`.
- `test/dvlnet_wire_test.cpp`, `test/network_peer.cpp`,
  `test/run_network_scenario.py`, `CMake/NetworkHarness.cmake`,
  `tools/wiiu/package-network-peer.sh` — the harness and tests.
- `Source/platform/wiiu/keyboard.{cpp,hpp}`, `Source/DiabloUI/multi/selgame.cpp`
  — the earlier PC-text-input / empty-password changes.
- `Source/diablo.cpp`, `Source/menu.cpp` — the `skip-intro` dev marker.
- Locally edited for tests: Cemu SD `network_peer.ini`; `diablo.ini`
  (`Previous Host=127.0.0.1`, Intro off, pause off) and
  `controllerProfiles/controller0.xml` for the manual UI path.

### Afterwards

4. **Polish** — `DEVILUTIONX_GAMEPAD_TYPE Nintendo`, TV/DRC docking, HOME/ProcUI.
5. **Then the cleanup below**, which is deliberately deferred: the debug output
   is the main tool for building the feature.

### Deferred cleanup

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
| `Source/platform/wiiu/keyboard.cpp` | Text input: enables SDL text events (no swkbd) |
| `Source/platform/wiiu/random.cpp` | libsodium RNG backend (no OS RNG on the console) |
| `Source/dvlnet/spawned_monster_delta.hpp` | ABI-independent delta wire codec (32-bit BE ↔ 64-bit LE) |
| `Source/dvlnet/tcp_server.cpp` / `tcp_client.cpp` | TCP provider: per-socket write queues, port-aware self-connect |
| `test/network_peer.cpp` | No-UI dvlnet TCP peer (host/join, turns, messages, socket probe) |
| `test/run_network_scenario.py` | Orchestrates desktop ↔ desktop scenarios (players, password, drop, port) |
| `test/dvlnet_wire_test.cpp` | Golden packet/frame vectors, encryption, spawned-monster codec |
| `CMake/NetworkHarness.cmake` | `BUILD_NETWORK_HARNESS=ON` target (independent of `BUILD_TESTING`) |
| `tools/wiiu/package-network-peer.sh` | Packages the harness as a standalone WUHB |
| `tools/wiiu/cemu-run.sh` | Launches Cemu with SIGPIPE ignored (multiplayer needs it) |
| `Source/utils/paths.cpp` | Base/assets/pref path resolution (SDL) |
| `Source/main.cpp` | Calls `wiiu_init()` |
| `CMake/platforms/wiiu.cmake` | Platform build config (`NONET=OFF`, ZeroTier off, sound on) |
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
| Cemu MLC | Cemu's own setting; override with `WIIU_MLC` |
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
