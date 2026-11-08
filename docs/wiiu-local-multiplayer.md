# Wii U Local Multiplayer (2-4 players, one screen)

Design analysis. **No code changed.** Written after reading the actual source, every claim carries a file:line reference.

---

## TOC

- [1. Executive summary](#1-executive-summary)
- [2. Why the menu says "Offline"](#2-why-the-menu-says-offline)
- [3. How multiplayer works today](#3-how-multiplayer-works-today)
- [4. The four layers that must change](#4-the-four-layers-that-must-change)
- [5. Options](#5-options)
  - [5.1 Where the players come from (P)](#51-where-the-players-come-from-p)
  - [5.2 Camera and leash (C)](#52-camera-and-leash-c)
  - [5.3 Input routing (I)](#53-input-routing-i)
  - [5.4 Menu flow (U)](#54-menu-flow-u)
- [6. Recommendation](#6-recommendation)
- [7. Risks and unknowns](#7-risks-and-unknowns)
- [8. Suggested milestones](#8-suggested-milestones)
- [9. Facts worth keeping](#9-facts-worth-keeping)

---

## 1. Executive summary

**The single most important finding: the engine already runs single player on top of the multi‑player network layer.**

`InitSingle()` and `InitMulti()` are nearly the same function. Single player uses the *same* provider (`SELCONN_LOOPBACK`) and the *same* `SNetCreateGame()` call — it just asks for 1 player slot and leaves `gbIsMultiplayer` false.

```
InitSingle()                          InitMulti()
────────────                          ────────────
Players.resize(1)                     Players.resize(MAX_PLRS)   ← 4
SNetInitializeProvider(LOOPBACK)      SNetInitializeProvider(chosen)
SNetCreateGame("local","local",…)     UiSelectGame(…) → playerId
MyPlayerId = 0                        MyPlayerId = playerId
gbIsMultiplayer = false               gbIsMultiplayer = true
```

So this is **not** "bolt networking onto a single-player engine". It is "give the existing net abstraction a second local player".

**The four things that are genuinely missing:**

| # | Missing | Size |
|---|---|---|
| 1 | A net provider that serves > 1 **local** player | M |
| 2 | Per-controller input routing (today every pad drives the one player) | M |
| 3 | Camera that follows more than one player (+ a leash) | S–M |
| 4 | A menu entry that is not "Offline" | S |

**Recommendation: Option B** — a new `local_coop` net provider, a shared camera on the party centroid, and a soft leash. Ship 2 players first, make the count a setting, 4 is the engine cap.

Deliberately **not** doing: split-screen (touches the renderer, the panel and the UI for every screen) and real network play (`NONET`, ZeroTier, TCP) — see [§5](#5-options).

---

## 2. Why the menu says "Offline"

`Source/DiabloUI/multi/selconn.cpp:28`

```cpp
const char *ConnectionNames[] {
    "ZeroTier",
    N_("Client-Server (TCP)"),
    N_("Offline"),            // ← SELCONN_LOOPBACK — a NET PROVIDER, not "off"
};
```

`SelconnLoad()` (`selconn.cpp:56`) only adds ZeroTier and TCP `#ifndef NONET`. Our build sets `NONET=ON`, so **"Offline" is the only entry left** — it is not a message that multiplayer is disabled:

```cpp
#ifndef NONET
    … ZeroTier, TCP …
#endif
    vecConnItems.push_back(… SELCONN_LOOPBACK …);   // always added
```

And what the UI says about it (`selconn.cpp:133`):

```cpp
case SELCONN_LOOPBACK:
    _("Play by yourself with no network exposure.");   // ← what you saw
    players = 1;
```

**"Offline" is the loopback provider** — the local, no-network path. That is precisely the provider single player already uses. It is the natural place to grow a local co-op provider from.

```
                    MakeNet(provider)
                           │
       ┌───────────────────┼───────────────────┐
       │                   │                   │
   ZeroTier            TCP client          loopback
   (NONET: gone)     (NONET: gone)     (always present)
                                             │
                                    ┌────────┴────────┐
                                    │                 │
                              single player      ← grow this
                              (creates 1)          into local
                                                   co-op (2–4)
```

---

## 3. How multiplayer works today

### 3.1 The stack

```
┌──────────────────────────────────────────────────────────────┐
│  controls/plrctrls.cpp    input → NetSendCmd*(CMD_*)         │
│  controls/devices/joystick.h   Joystick::All() — merged      │
└───────────────────────────┬──────────────────────────────────┘
                            │  SNetSendMessage(dest, …)
┌───────────────────────────▼──────────────────────────────────┐
│  storm/storm_net.cpp      thin forwarder + mutex             │
└───────────────────────────┬──────────────────────────────────┘
┌───────────────────────────▼──────────────────────────────────┐
│  dvlnet/abstract_net.h    ~20 pure virtuals                  │
│    create() join() SNetSendMessage() SNetReceiveMessage()    │
│    SNetSendTurn() SNetReceiveTurns() SNetGetProviderCaps() … │
│    └── loopback.cpp   zerotier   tcp_client                  │
└───────────────────────────┬──────────────────────────────────┘
┌───────────────────────────▼──────────────────────────────────┐
│  msg.cpp   CMD_* protocol, ProcessMessages()                 │
│  multi.cpp Players[MAX_PLRS], MyPlayer, MyPlayerId           │
│  nthread.cpp  background thread, turn-based lockstep         │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 The loopback provider is a "pseudo multiplayer" stub

`Source/dvlnet/loopback.cpp` — worth reading in full, it is short and its comments say what it is:

```cpp
int loopback::create(std::string_view)  { IsLoopback = true; return plr_single; }  // plr_single = 0
int loopback::join(std::string_view)    { ABORT(); }

bool loopback::SNetSendMessage(uint8_t dest, void *data, size_t size)
{
    if (dest == plr_single) { …push to message_queue… }   // only ever talks to itself
    return true;
}
bool loopback::SNetReceiveTurns(…) { /* all slots null */ }
bool loopback::SNetSendTurn(…)     { return true; }       // no-op
bool loopback::SNetGetProviderCaps(…) { caps->maxplayers = MAX_PLRS; }  // "capped to 4"
```

Comments in the file: *"not called in real singleplayer mode / not needed in pseudo multiplayer mode (?)"*.

So today the local player's commands are **sent into the net layer and looped straight back** — the engine then applies them via the normal `msg.cpp` path. That loop is the hook for local co-op.

### 3.3 Engine cap is 4 players

```cpp
Source/multi.h:26     #define MAX_PLRS 4
```

`caps->maxplayers = MAX_PLRS; // capped to 4` — **4 is a hard engine limit.** More than 4 would be a much bigger project (inventories, panels, save layout, protocol).

### 3.4 Today there is exactly one local player

```cpp
Source/player.h:917   extern uint8_t MyPlayerId;
Source/player.h:918   extern Player *MyPlayer;
Source/multi.cpp:534  MyPlayerId = 0;      MyPlayer = &Players[0];       // InitSingle
Source/multi.cpp:565  MyPlayerId = playerId; MyPlayer = &Players[playerId]; // InitMulti
```

`MyPlayer` drives **camera** and **UI panels**:

```cpp
Source/player.cpp:2539   ViewPosition = MyPlayer->position.tile;    // camera follows "me"
```

Every other player slot is expected to be *remote* — driven by `CMD_*` messages.

### 3.5 Input is single-player by construction

```cpp
Source/controls/control_mode.cpp:6    ControlTypes ControlDevice = ControlTypes::None;  // ONE global
Source/controls/devices/joystick.h:35 static const std::vector<Joystick> &All();
Source/controls/devices/joystick.h:36 static bool IsPressedOnAnyJoystick(ControllerButton);
```

There is a vector of all connected pads, but the engine only ever asks *"is this button pressed on any pad"*. It has no concept of "pad 2 is player 2".

### 3.6 A multiplayer guard that concerns us

`Source/menu.cpp:82`

```cpp
bool InitMultiPlayerMenu()
{
#ifndef _DEBUG
    if (IsAssetIntegrityViolated || HasLooseLogicAssets()) {
        UiSelOkDialog(_("Multi Player Game"), _("Cannot play Multiplayer with overridden *.lua, *.tsv, or *.sol assets."), false);
        return true;
    }
#endif
    gbIsMultiplayer = true;
    …
}
```

`HasLooseLogicAssets()` (`Source/engine/assets.cpp:~772`) scans the override paths — which **always include `<PrefPath>`** (`assets.cpp:745`) — for loose `.lua` / `.tsv` / `.sol` files.

> Practically: keep `sd:/wiiu/apps/devilutionx/` free of loose script/tsv files, or multiplayer silently refuses to start. Our Hellfire mod is *packed* inside the WUHB, so we are fine today.

---

## 4. The four layers that must change

```
        ┌─────────────────────────────────────────────────────┐
   (4)  │ UI      "Multiplayer" → new "Local Co-op" entry      │
        └─────────────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────────────┐
   (3)  │ CAMERA  follow the party, not just me   +  leash    │
        └─────────────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────────────┐
   (2)  │ INPUT   pad N → player id N → CMD_*(playerId, …)    │
        └─────────────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────────────┐
   (1)  │ NET     local_coop provider: N local players         │
        └─────────────────────────────────────────────────────┘
```

Layer 1 is the enabler, layer 2 is the bulk of the work, layers 3 and 4 are small.

---

## 5. Options

### 5.1 Where the players come from (P)

| | Option | What it means | Cost | Verdict |
|---|---|---|---|---|
| **P0** | Do nothing | "Offline" stays 1 player | — | ✗ |
| **P1** | **`local_coop` provider** — a variant of `loopback` that owns N local player ids; each inline `Player` is a real local player whose commands are injected locally instead of arriving over a wire | Reuses `msg.cpp`, `Players[]`, the lockstep thread with zero changes to the simulation | **M** | ✅ **recommended** |
| **P2** | Make the engine aware of several `MyPlayer`s (local UI/camera per player) | Touches `MyPlayer` everywhere it is used (hundreds of sites) | **XL** | ✗ |
| **P3** | Spawn N internal clients in-process | N copies of game state; large memory use, fragile | **XL** | ✗ |

**Why P1 is the right shape:** the engine already treats player slots 1..3 as "other people whose commands I receive". A local co-op provider just has those commands originate from a second controller instead of a socket. Nothing in the simulation or the save format needs to change.

### 5.2 Camera and leash (C)

Diablo's camera is one viewport centred on `MyPlayer`. With N players on one screen, *something* must decide where the camera looks and what happens when someone walks off.

```
C1  follow P1 only                C2  follow party centroid          C3  split-screen
┌───────────────┐                 ┌───────────────┐                   ┌───────┬───────┐
│ P1 ●          │                 │      ●        │                   │ P1 ●  │  ● P2 │
│   P2 ● (offscreen!)             │   P1 ● P2 ●   │                   │       │       │
└───────────────┘                 └───────────────┘                   └───────┴───────┘
simple, feels unfair              balanced, 1 camera                   2 cameras, 2× render,
need hard leash                   still needs a leash                  UI/panel rework for all screens
```

| | Option | Cost | Notes |
|---|---|---|---|
| **C1** | Camera on P1 + hard leash on the others | **S** | Easiest; P1 gets an unfair advantage |
| **C2** | **Camera on party centroid + soft leash** | **S–M** | Fair; the leash becomes natural: "you can't leave because there is nowhere to go" |
| **C3** | Split-screen (2/4 viewports) | **XL** | Renderer, panel, cursor, all DiabloUI screens assume one 640×480 surface |
| **C4** | GamePad screen = P1, TV = the rest | **L** | Wii U exclusive, cute, but a second render path + TV/DRC work we skipped |

**Leash mechanics** (independent of camera choice):

```
hard block   player cannot walk past the view edge        (simplest, slightly clunky)
soft push    camera re-centres, out-of-view player is      (smoother)
             pulled / "must stay together" message
```

> Worth noting: the visible play area is roughly 640×350 px (~20×11 tiles) because the panel takes the bottom third. The leash radius is therefore genuinely small — this is a "stay together" game, not a "wander freely" game. That is exactly the design the user asked for.

### 5.3 Input routing (I)

| | Option | Cost | Notes |
|---|---|---|---|
| **I0** | Do nothing (any pad drives P1) | — | fine for 1 player |
| **I1** | **Pad index → player id** (pad 0 = P1, pad 1 = P2, …) | **M** | deterministic, no join flow |
| **I2** | "Press A to join" — first come first served, hot-plug aware | **M+** | nicer UX, more state |
| **I3** | GamePad is always P1, TV pads are P2.. | **M** | good fit for the Wii U, ties into C4 |

All of these need the same underlying change: `NetSendCmd*(…)` currently stamps `MyPlayerId` in most places (`plrctrls.cpp:535,557,2562`) but already takes a player id in others (`plrctrls.cpp:1747,1769` pass `player.getId()`). So the command layer is close to ready; the device → player mapping is what is missing.

### 5.4 Menu flow (U)

```
U1  new entry in the existing list        U2  repurpose "Offline"          U3  skip menus entirely
┌───────────────────────────┐             ┌───────────────────────────┐    Multi Player → co-op
│ Multi Player Game         │             │ Multi Player Game         │    with N = saved setting
│  Select Connection        │             │  Select Connection        │
│   • Local Co-op (2-4)  ←  │             │   • Offline (1 player)    │
│   • Offline               │             │       ↑ now means co-op   │    least UI work
└───────────────────────────┘             └───────────────────────────┘    least discoverable
fits the existing pattern                 confusing: name lies
(`MakeNet` already switches on provider)
```

**U1 is recommended** — it matches how the code is already written: `MakeNet(provider)` switches on a provider id, and `SelconnFocus()` already sets a per-provider description *and* player count (`players = MAX_PLRS` / `1`). Adding a "Local Co-op" entry means adding one enum value, one string, and one description — no restructuring.

---

## 6. Recommendation

**P1 + C2 + I1 + U1**, 2 players first.

```
  Multi Player
     └─ Select Connection
          ├─ Local Co-op (2-4)   ← new
          └─ Offline (1 player)
                  │
                  ▼
          players = 2            ← setting, default 2, max 4 (MAX_PLRS)
                  │
                  ▼
          local_coop provider
             ├─ local players share one world, one camera (centroid)
             ├─ pad 0 → player 0, pad 1 → player 1, …
             ├─ soft leash: nobody leaves the viewport
             └─ saves go to multi_N.*  (own namespace — single player untouched)
```

**Why this one:**

1. **It is the smallest change that produces a playable game.** P1 needs one new `abstract_net` subclass modelled on `loopback.cpp`; C2 is a camera target; I1 is a mapping table; U1 is one menu entry.
2. **It reuses everything already proven.** `msg.cpp`, `Players[]`, the lockstep thread, and the multiplayer save namespace are all existing, working code paths — the same ones single player already runs through.
3. **It does not foreclose anything.** Split-screen (C3) and hybrid remain possible later: they are camera/render work on top of the same multi-local-player foundation. Choosing C2 now costs nothing if we later add a second viewport.
4. **It gives the user exactly the requested design** — "nobody can wander off without the others" *is* the natural consequence of one shared camera plus a small viewport.
5. **`multi_*.sv` keeps risk low** — local co-op characters live in their own save namespace, so the existing single-player and Hellfire saves cannot be damaged while the feature is being built.

**Explicitly deferred:**

| Deferred | Why |
|---|---|
| Split-screen (C3) | 2–4× render cost and a rework of every DiabloUI screen; a separate project after co-op works |
| GamePad-as-second-screen (C4) | needs TV/DRC work not yet done |
| Real network multiplayer | `NONET=ON`; needs `nsysnet`, provider work, and the multiplayer asset-integrity guard resolved first |
| > 4 players | `MAX_PLRS 4` is baked into the protocol, panels and save layout |

---

## 7. Risks and unknowns

| Risk | Impact | Note |
|---|---|---|
| **Input path is global, not per player** | main effort | `ControlDevice`/`Joystick::All()` assume one player; this is layer 2 and the bulk of the work |
| **`gbIsMultiplayer = true` changes rules** | expected, but must be verified | multiplayer quests, difficulty scaling, `multi_*` saves, `IsLoopback` conditionals (`multi.cpp:293,895,953`) |
| **Turn/lockstep thread with zero latency** | unknown | `nthread.cpp` paces the game in ticks assuming real latency. With local players it may run too fast/slow — needs measuring, possibly a local fast-path |
| **Camera centroid edge cases** | medium | dungeon stairs, portals, town, death, level transitions all currently assume "centre on me" |
| **Players on different dungeon levels** | scope decision | the engine supports it in MP; a shared camera cannot. Must be forbidden (leash) or handled |
| **UI panels show one player** | acceptable for v1 | inventory/character screens are per-`MyPlayer`; other players use their own pad, so each needs its own way to open panels |
| **Asset-integrity guard** | low | keep the SD data dir free of loose `.lua`/`.tsv`/`.sol` |

---

## 8. Suggested milestones

```
M1  provider skeleton      local_coop serves 2 ids; a 2nd player exists in the
                           world but is a statue (no input). Proves the net hook.   ← Cemu
M2  input routing          pad 2 drives player 2; both move. Camera still on P1.   ← Cemu
M3  camera + leash         centroid camera, soft leash, level-change handling.     ← Cemu
M4  menu                   "Local Co-op (2-4)" entry, player count setting.        ← Cemu
M5  make it 4              make the count a setting, verify MAX_PLRS path.         ← Cemu
M6  hardware pass          carry the SD card, verify pads on a real Wii U.         ← Wii U

Each milestone is playable and testable on Cemu — no SD card round-trips needed
until M6.
```

---

## 9. Facts worth keeping

| Fact | Where |
|---|---|
| Engine player cap is **4** | `Source/multi.h:26` `#define MAX_PLRS 4` |
| Single player and multiplayer share one code path | `Source/multi.cpp:519` `InitSingle`, `:544` `InitMulti` |
| Single player already uses the loopback net provider | `Source/multi.cpp:523` |
| Provider selected by one enum, switched in one place | `Source/dvlnet/abstract_net.cpp:22` `MakeNet` |
| Loopback only ever sends to itself, turns are a no-op | `Source/dvlnet/loopback.cpp` |
| With `NONET` the provider is *always* loopback | `Source/dvlnet/abstract_net.cpp:23` |
| "Offline" is `SELCONN_LOOPBACK`, not "multiplayer off" | `Source/DiabloUI/multi/selconn.cpp:28–31, 64, 133–135` |
| Camera follows exactly one player | `Source/player.cpp:2539` |
| Input device is one global | `Source/controls/control_mode.cpp:6` |
| All pads are merged into one input stream | `Source/controls/devices/joystick.h:36` |
| Commands already take a player id in some paths | `Source/msg.h:743` `NetSendCmdLoc(uint8_t playerId, …)` |
| Multiplayer refuses to start on loose `.lua`/`.tsv`/`.sol` | `Source/menu.cpp:85`, `Source/engine/assets.cpp:~772` |
| Save namespace for MP is separate (`multi_*`) | `Source/pfile.cpp:81` |
