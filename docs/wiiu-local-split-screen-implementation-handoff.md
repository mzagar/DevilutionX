# Wii U local split-screen: implementation handoff

## Executive summary

DevilutionX does not currently support four local players or independent split-screen views in one application. SDL can combine several images, but it does not provide the missing game-state, input, camera, or save isolation.

For independent player movement between levels, recommend one application with up to four isolated client contexts and an in-memory multiplayer transport. Reuse existing multiplayer rules. Share immutable assets, one input pump, one compositor, and one audio device. This is a major shared-engine refactor, not a small Wii U adapter.

Accept this architecture only after two-context and four-context tests meet measured Wii U memory and timing limits. A shared simulation with all players on one level is a smaller alternative. It changes gameplay and requires explicit approval.

Prove production logic without manual UI interaction first. Test rendering offscreen next. Run scripted target diagnostics before the final manual UI smoke test.

**Source snapshot:** `74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61`, branch `master`. Research date: 2026-11-13.

**Status:** Research and implementation proposal. No split-screen feature, runner, or project test was implemented. A small desktop SDL composition probe passed. No Wii U game runtime test ran.

**Starting point:** The port owner reports working Wii U single-player, sound, and saves with full Diablo and Hellfire MPQs. That working port is not in this checkout. Preserve it and inspect its actual changes before integration.


## Implementation status (branch `wiiu-splitscreen`, Nov 2026)

The research phase is over; the first playable slice exists. The recommended
multi-context architecture is **not** what shipped first. The branch took the
cheaper shared-world route (the "restricted alternative" of §3.3) to get a
playable build in Cemu early:

- `InitLocalCoop(seats)` starts a no-UI local session on the loopback provider
  with one hero save per seat, shared world, shared camera, single-player rules.
- Commands from seat N are delivered locally with sender N
  (`SNetSendMessageAs`) and the packet header describes the acting player, so the
  engine applies each command to the right `Players[]` slot.
- Per-seat input ownership: `InputSeats` (device instance ID -> seat, with
  generation) and `SeatInput` (per-seat buttons/axes); seat 0 no longer reads
  other pads' motion state. `plrctrls_after_game_logic()` drives seats 1..N
  through the production walk/attack path.
- Console switch: `<pref>/local_coop.ini` (`[LocalCoop] Seats=2..4`, optional
  `AutoStart=1`).
- Verified in Cemu: two players play together (GamePad + Wii U Pro Controller);
  a four-seat session boots.

Split screen now exists: `SplitScreen=1` gives each seat its own viewport and
camera. `CalculateViewLayout` (pure, tested) splits the viewport into one, two or
a 2x2 grid of cells with the shared 640-wide main panel centred at the bottom;
each seat renders at its cell size with a local copy of the viewport geometry
(§6.2), so there is no cropping or rescaling. Verified in Cemu on 17 Nov 2026 at
1280x720 (2 seats: 640x592 cells; 4 seats: 640x296 cells).

Not done: compact per-seat HUDs and panels (there is only a name + HP/MP text
placeholder per view; the main panel and inventories are seat 0's), per-seat
vision/palette isolation in dungeons, independent travel, seat-aware
pickups/doors/spells, and the multi-context isolation that independent levels
require. The next step is §6.4: compact HUD geometry per cell and per-seat local
panels, then the remaining §5 seat actions.

## Contents

1. [Scope and decisions](#1-scope-and-decisions)
2. [What the current code supports](#2-what-the-current-code-supports)
3. [Architecture options and recommendation](#3-architecture-options-and-recommendation)
4. [Client contexts, local transport, and scheduling](#4-client-contexts-local-transport-and-scheduling)
5. [Controllers and local actions](#5-controllers-and-local-actions)
6. [Views, composition, HUD, and sound](#6-views-composition-hud-and-sound)
7. [Gameplay, heroes, and persistence](#7-gameplay-heroes-and-persistence)
8. [Wii U integration and file map](#8-wii-u-integration-and-file-map)
9. [Non-UI test strategy](#9-non-ui-test-strategy)
10. [Build integration and commands](#10-build-integration-and-commands)
11. [Implementation sequence and release gates](#11-implementation-sequence-and-release-gates)
12. [Evidence, limits, and source index](#12-evidence-limits-and-source-index)

## 1. Scope and decisions

### 1.1 Confirmed request

- Run the game on one Wii U, with up to four local players.
- Give each player a separate hero, controller, and split-screen view.
- Follow the existing shared-code and platform-adapter pattern.
- Compare options before implementation.
- Test real production logic without manual game-menu interaction.
- Make manual UI smoke testing the last validation step.

This document defines an implementation path. It does not certify that four-client performance is sufficient on Wii U.

### 1.2 Proposed product contract

The user has not specified every rule below. Confirm these decisions before implementation. They are recommendations, not existing features.

| Topic | Recommended initial contract | Decision or limit |
|---|---|---|
| Players | One to four fixed seats, each with one distinct hero. | Select the roster before loading. Defer live hero replacement and drop-in unless separately tested. |
| Display | Put all views in one TV composite. Use the GamePad as a controller. | Independent GamePad video is optional, not part of the initial design. |
| Travel | Preserve independent stairs, portals, and level travel. | This avoids a hidden restriction. Same-level party travel is a separate lower-cost option. |
| Rules | Reuse normal multiplayer rules for one common Diablo or Hellfire mode. | Do not merge four single-player campaigns. Test full-quest settings separately. |
| Controllers | First validate GamePad plus up to three Pro Controllers. | Also test four Pro Controllers with a connected GamePad before claiming that combination. |
| Menus | Inventory, spell selection, automap, and shops belong to one seat. | A personal menu does not pause other players. |
| Pause | An explicit local-session pause freezes all client simulations. | Define controller-loss behavior. Continue hardware/lifecycle polling while paused. |
| Saves | Persist each multiplayer hero to its own archive. | A resumable shared dungeon is a separate feature. |
| Stash | Use an explicit shared-household or per-profile policy. | Recommended first policy: one serialized household stash with separate seat UI state. |
| Sound | Use one mixer and one music policy. | Deduplicate replicated world events. Do not mix four full soundtracks. |
| External network | Local play needs no Internet, other console, or TCP host. | Local-plus-online play is not required here. It needs separate protocol and compatibility validation. |
| Mods | Preserve a tested default-content path first. | Context-dependent Lua/mod support needs an explicit compatibility policy. |

A *seat* is a local player's controller and view assignment. A *client context* is one isolated instance of mutable game/session state. Neither is an operating-system process.

### 1.3 What “non-UI first” means

Do not start development by repeatedly opening menus and joining a game manually. Expose startup, actions, simulation, saves, and shutdown through production APIs.

UI state machines still need automated tests. Rendering still needs offscreen tests. Physical controllers and performance still need target diagnostics. These checks happen before manual gameplay smoke testing.

Later, make the lobby and hero-selection UI a thin adapter to the tested session descriptor. Test its validation and seat/hero assignments without drawing. The final smoke test must not be the first test of UI-to-session integration.

## 2. What the current code supports

### 2.1 Four network players, one local owner

`MAX_PLRS` is four. However, one process has one `MyPlayerId`, one `MyPlayer`, and one active world.

```text
Current application
  one SDL event stream -> one local input state -> one local hero
                                               -> one SNet endpoint
  Players[0..3] = local hero + remote representations
  one loaded level -> one camera/HUD -> one indexed image -> one present
```

`InitMulti()` allocates four player records but loads only one selected local hero (`Source/multi.cpp:544–572`). `InitSingle()` allocates one. `NetInit()` initializes one provider and one turn scheduler (`multi.cpp:830–889`).

The current loopback provider does not implement four local players. It always uses player zero, has no real turn exchange, and aborts on `join()` (`Source/dvlnet/loopback.cpp:12–58`). Its reported maximum player count is not local-co-op support.

### 2.2 Global state prevents simple duplication

| Area | Current evidence | Consequence |
|---|---|---|
| Hero identity | `player.cpp:64–68`: `Players`, `MyPlayerId`, `MyPlayer`, death state. | Input and views cannot select independent owners safely. |
| Player references | `Player::getId()`, `player.cpp:1589–1592`, subtracts from global `Players`. | A player from context A must never resolve against context B's storage. |
| Active world | `levels/dun_tile_data.hpp:31–118`; global monsters/items/objects/missiles. | One extra camera cannot show an unloaded live level. |
| Level membership | `Player::isOnActiveLevel()`, `player.h:848–854`. | Simulation uses global level identity. |
| Network | `storm/storm_net.cpp:29–37`: one provider, public/private state, and mutex. | Multiple local endpoints require ownership changes. |
| Input | `controls/controller_motion.cpp:128–130`; `controller.cpp:77–93`. | Devices share axes and aggregate button/modifier queries. |
| Rendering | `engine/dx.cpp:47–61`; `scrollrt.cpp:1315–1459`. | One palette, image, camera, HUD, and local UI state. |
| Saves | `pfile.cpp:527–543,645–648`; `loadsave.cpp:2339–2416`. | One save slot and local owner leak through nested operations. |
| RNG and scripting | `engine/random.cpp:13–20`; `lua/lua_global.cpp`. | Contexts cannot share mutable gameplay RNG or implicit Lua owner state. |

A whole-word search for `MyPlayer` or `MyPlayerId` finds 753 matching lines across 61 files under `Source/`. These are search counts, not an edit estimate. The feature crosses the engine, not only the Wii U platform directory.

### 2.3 `MyPlayer` controls gameplay authority

This pointer does not only select a camera.

| Behavior | Existing owner rule | Failure from “four inputs plus four cameras” |
|---|---|---|
| Damage | `MonsterAttackPlayer()`, `monster.cpp:1222–1227`, applies authoritative damage only to `MyPlayer`. | Other locally controlled heroes can follow the wrong damage path. |
| XP | `_addExperience()` and `AddPlrMonstExper()`, `player.cpp:2407–2459`. | Only the implicit local owner receives its normal award. |
| Durability | `player.cpp:923–995`. | Equipment changes depend on the local owner. |
| Death and resurrection | `player.cpp:2659–2784`; `msg.cpp:1919–1950`. | Drops or resurrection messages can be missing or duplicated. |
| Item pickup | `msg.cpp:1431–1584`; `player.cpp:1996–2015`. | Arbitration and successful inventory writes assume one endpoint owner. |
| Item drop | One `ItemLimbo`, `msg.cpp:304,1602–1636`. | Concurrent local drops can overwrite transient state. |
| Command filtering | One `lastSentPlayerCmd`, `msg.cpp:307,400–415`. | Identical actions from two local actors can interfere. |

`NetSendCmdLoc(playerId, ...)` uses `playerId` as the **destination**, not the actor (`msg.cpp:3011`; `multi.cpp:215–226`). The endpoint supplies sender identity. Passing another seat number does not make that hero send a command.

Do not implement split screen by changing `MyPlayer` before each callback or draw call. Hidden statics, queued callbacks, borrowed pointers, workers, and save operations can retain the wrong owner.

### 2.4 Independent levels need independent live state

Each existing network process loads its own active level. Retained `DeltaLevels` hold changes for other levels (`msg.cpp:240–245,284`). They are not complete live worlds.

`DeltaLeaveSync()` saves changes. `DeltaLoadLevel()` reapplies them after generation (`msg.cpp:1016–1042,2961–2981`). Live missiles, AI state, graphics resources, palette state, and execution state are not all represented there.

`StartNewLvl()` changes local state and requests a global transition (`player.cpp:2887–2922`). `DoLoad()` releases the current world and loads another (`interfac.cpp:311–463`). Existing loading is not a per-viewport operation.

## 3. Architecture options and recommendation

### 3.1 Compare the options

| Option | Independent cameras | Independent levels | Main cost | Decision |
|---|---|---|---|---|
| Duplicate the final image or add SDL viewports only | No real independence | No | Small display change | Reject. Does not meet the request. |
| One shared camera | No split screen | Usually no | Still needs multiple owners and input state | Different feature. Do not substitute it. |
| One simulation, several local owners, one level | Yes, after refactor | No | Authority audit, actor commands, per-seat UI/vision, party transitions | Lowest expected duplication. Offer only with explicit same-level approval. |
| One authoritative session with several active level contexts | Yes | Yes | Redesign world ownership, authority, commands, quests, level ticking, and local ownership | Strong long-term resource model. Largest gameplay redesign. |
| Isolated client contexts with in-memory transport | Yes | Yes | Isolate mutable engine/session/presentation state; duplicate simulation for shared levels | Recommended full-feature candidate, subject to resource gates. |
| Several OS processes, TCP, frame capture/composition | Potentially | Yes | Process hosting, IPC, repeated resources, target deployment | Useful desktop reference. Wii U feasibility is not established. |
| Global swapping or macro/linker duplication | Appears possible initially | Unreliable | Hidden state and lifetime errors | Reject as the production design. |

### 3.2 Recommended full-feature candidate

Use one application with up to four explicit client contexts. Each context behaves like one existing multiplayer client. It owns one local hero and remote player representations.

```text
                            Application
          one SDL pump / devices / lifecycle / shared assets
                                |
                    Device -> Seat -> Client/Player
                                |
        +---------------+-------+-------+---------------+
        |               |               |               |
     Client A        Client B        Client C        Client D
     local hero A    local hero B    local hero C    local hero D
     own world       own world       own world       own world
     own RNG/UI      own RNG/UI      own RNG/UI      own RNG/UI
        |               |               |               |
        +---------------+---------------+---------------+
                   queued in-memory session transport
                                |
         per-client indexed image + per-client palette
                                |
             per-client RGB conversion -> TV compositor
                                |
                       one SDL present / mixer
```

This preserves existing endpoint ownership, turns, item arbitration, late-entry deltas, and independent level travel. It avoids designing a new authority model at the same time.

It does **not** avoid a major refactor. Four clients on the same level can simulate four replicas of that level. Assets and cached data can also multiply unless sharing is explicit.

Start with one isolated client. Then prove two clients in one process. Measure two and four clients on Wii U before completing the split-screen UI.

If this design exceeds the agreed limits, stop and report the result. Choose a shared multi-level engine or request approval for same-level party play. Do not silently remove independent travel.

### 3.3 Restricted alternative: one shared world

This option uses one simulation and several explicitly local owners. It can have four cameras without four simulation replicas.

It still needs owner-aware damage, XP, durability, death, item transactions, vendors, saves, and per-seat presentation. A generic replacement of every `MyPlayer` check is unsafe. Some checks select authority; others select presentation.

Party transitions need a state machine. Resolve conflicting stairs/portal requests, low-level entry restrictions, dead heroes, failed loads, and return-to-town behavior. Living players must not lose their world because one dead player selects recovery.

In this architecture, each active world ticks once per session tick. In the recommended client-context architecture, each client replica ticks once when ready. Neither architecture ticks the game once per rendered view.

## 4. Client contexts, local transport, and scheduling

All types and interfaces in this section are **proposals**. They do not exist at the analyzed commit.

### 4.1 Define ownership before moving code

| Owner | State |
|---|---|
| Application services | SDL lifetime, physical devices, output window/renderer, audio device, logging, storage coordinator, immutable asset cache. |
| Local session | Roster, seat bindings, session rules, in-memory bus, global pause, scheduler, result/error state. |
| Client context | `Players`, local identity, active level, entities, grids, seeds/RNG, quests, portals, deltas, pending commands, item transactions. |
| Client endpoint | Provider instance, callback registry, turn queues, counters, message buffers, deadlines, player-info and resync progress. |
| Local seat | Device generation, input/repeat state, local actions, hero binding, menu ownership. |
| Client presentation | Camera, cursor/target, panels, automap, visibility, palette/blending tables, derived render state. |
| Hero persistence | Save archive identity, mode, load metadata, hotkeys, vendor seed/stock, explicit stash policy. |
| Render scratch | Temporary lightmaps, clipping buffers, intermediate images. Reuse sequentially only when no references remain. |

Inventory file-scope variables, function-static variables, thread state, cached pointers, Lua state, and cleanup ownership. A new `ClientContext` struct alone does not make the engine reentrant.

Keep `Players` storage stable. `Player::getId()` and `Missile::sourcePlayer()` currently resolve through the global vector (`player.cpp:1589–1592`; `missiles.h:146–150`). Pass the owning context or use a context-tagged handle. Reject cross-context handles in debug builds.

Share immutable bytes only after proving their lifetime. Palette lookup tables, lazy caches, mutable resource handles, and mod state are not automatically immutable assets.

Use explicit context parameters in production boundaries. If temporary migration wrappers exist, forbid re-entry and escaped references. Remove them from the multi-client path before acceptance.

### 4.2 Keep game rules separate from transport

`gbIsMultiplayer` currently selects rules, save namespaces, pause behavior, and worker creation. `nthread_start()` starts a worker when it is true (`nthread.cpp:181–186`).

Introduce an explicit session policy. At minimum, distinguish content/rules, transport type, scheduler type, save mode, and pause policy. Local multiplayer rules must not imply a TCP connection.

The current `NONET` factory always chooses the one-player loopback provider. Simply setting `NONET=ON` will not enable local co-op.

A first implementation can retain multiplayer engine code and select a new local provider with external transports disabled. Then separate build capabilities so local play does not require sockets, Asio, or ZeroTier.

Audit compression and crypto dependencies during that separation. For example, packet cookie generation can use `randombytes` when `PACKET_ENCRYPTION` is enabled. Do not claim a reused path is dependency-free without tracing it.

An in-process bus does not need encrypted private-game packets. Keep normal external-network encryption intact. The separate Wii U network handoff (not part of this repository) documents external protocol and endian risks. Local same-build tests do not prove cross-platform wire compatibility.

### 4.3 Local transport contract

`dvlnet::base` already stores per-object queues and turn state. However, it still reads global `Players.size()` and SDL time (`base.cpp:243,288,306,523`). Give it an explicit roster/capacity and clock.

Refactor the SNet-facing ownership boundary. Each client needs its own endpoint and callbacks. Keep single-client wrappers for existing callers during migration.

The local transport must:

1. Bind sender identity to the endpoint, not to untrusted command payloads.
2. Preserve directed, self, and `SNPLAYER_OTHERS` message behavior.
3. Copy outgoing bytes into owned queues. Do not send gameplay pointers.
4. Preserve connection, game-info, create, leave, and drop events.
5. Preserve turn sequence, readiness, ordering, and wrap behavior.
6. Preserve the native `int32_t` turn API contract. Do not add an extra endian swap at that boundary.
7. Define receive-buffer lifetimes. Copy data that must survive the next receive call.
8. Bound queues and report overflow or no-progress failure.
9. Deliver callbacks under the receiving context's ownership.
10. Never call another engine context recursively while sending or holding a provider lock.

Reuse production message/turn behavior. Do not implement a second combat or inventory model in the transport.

Test delivery and remote state changes, not only successful send return values. Include reordered scheduling, sequence wrap, invalid destinations, endpoint removal, queue pressure, and restart.

### 4.4 Cooperative scheduler

Use one thread to own mutable client execution initially. SDL rendering also stays on its required thread. Do not start four engine threads while shared state remains.

`game_loop()` waits through `multi_handle_delta()` before `GameLogic()` (`diablo.cpp:3493–3512`). `GameLogic()` includes input, simulation, sound, triggers, quests, redraw, saves, and controller updates (`:1524–1570`). Extract this real path. A test loop over `ProcessPlayers()` alone is not equivalent.

The existing worker advances turns during level loading. `nthread_ignore_mutex` brackets parts of loading (`diablo.cpp:852–861,874–889`). Removing the worker without replacement can stall other clients.

Use bounded progress operations for startup, join, player-info transfer, resync, loading, and shutdown. `msg_wait_resync()` currently waits through `UiProgressDialog` (`msg.cpp:2733–2765`). It needs a non-UI progress boundary.

```text
One scheduler round
  poll hardware once
  route events and submit seat actions
  advance every client's pending start/load/resync operation
  submit each client's due turns and messages
  pump queued transport work within a budget
  consume messages and step ready clients through production logic
  repeat bounded progress if needed; fail on an expired deadline
  update presentation and collect semantic audio events
  optionally render views; compose and present once
```

Do not sequentially call four blocking `StartGame()` loops. Client A must not wait for B while B cannot run.

Use separate concepts for simulation time, presentation time, and monotonic service deadlines. During whole-session pause, freeze all client simulations. Continue input and lifecycle polling. Define whether local join/resync deadlines are suspended or continue. Avoid a catch-up burst on resume.

Preserve the configured tick rate. The default is 20 ticks/s (`Source/options.cpp:843`). Display frame rate is separate.

### 4.5 Context isolation tests

Before building a lobby, create two contexts with different heroes, seeds, levels, palettes, and save bindings. Alternate operations in A/B and B/A order.

Save or destroy A while B remains active. Recreate A. Verify that B's gameplay, callbacks, RNG, assets, and input remain valid.

The four-client gate must cover both one shared level and four different levels. Reunite the heroes and verify retained changes, item ownership, quests, and portals.

Client replicas are not necessarily byte-identical at every wall-clock instant. Compare documented semantic state at synchronized checkpoints. Exclude local camera/UI data and private town stock where appropriate.

### 4.6 Shared serialization defects still affect local peers

A local bus removes sockets. It does not remove encoding inside gameplay commands. Wii U is big-endian. Two Wii U contexts can still fail when a writer encodes a field and the receiver does not decode it.

Resolve these source-confirmed risks before accepting the real-engine scenarios. The findings below are static evidence, not executed game regressions.

| Existing path | Risk | Required regression |
|---|---|---|
| Spawn commands: `msg.cpp:2607–2636,2995–3009` | The writer encodes `seed` as little-endian. The reader uses raw `message.seed` for the delta and spawned monster. | Use seed `0x12345678`. Check the spawned entity and retained delta on Wii U, including reunion after level travel. |
| Item validation: `pack.cpp:450` and `msg.cpp:1306` | Both paths apply `Swap16LE` to the 32-bit `dwBuff` field. The writer uses `Swap32LE` at `msg.cpp:2691`. | Test both packing and dropped-item validation. Include bits in both halves, `CF_HELLFIRE`, and a Hellfire spell book. |
| Monster sync: `sync.cpp:279,293` | Processing decodes `wLen`, but the assertion checks the raw encoded field. | Send 26 ten-byte records. Decoded length 260 is valid; the raw big-endian value 1025 must not drive the assertion. |
| Leave callback: `dvlnet/base.cpp:129–151`, `multi.cpp:474–489` | The current provider passes a native leave reason. The engine swaps it again. | Define one callback contract. Check EXIT, ENDING, and DROP through the local endpoint and shared handler. |
| Spawned-monster delta: `msg.cpp:231–245,654–701` | A raw struct includes `size_t` and padding. Its layout is 12 bytes on PPC32 and 16 bytes on a 64-bit desktop. | Same-executable peers share an ABI, so this size mismatch alone is not a local-peer blocker. Use explicit fixed-width serialization before promising external mixed-architecture compatibility. |

Do not bypass production validation to make the tests pass. Fix shared producers and consumers, and keep the normal network regression tests. These checks are separate from controller, viewport, and context isolation.

## 5. Controllers and local actions

### 5.1 Current problems

The device registry already stores controller instance IDs (`controls/devices/game_controller.cpp:178–260`). However, `IsPressedOnAnyController` merges state. `controller.cpp:77–93` uses that merged result for modifiers and buttons.

`PollEvent()` changes motion state before higher-level dispatch (`controls/input.h:15–29`). Routing only in `GameEventHandler` is too late.

Other shared state includes:

- One axes set in `controls/controller_motion.cpp:128–130`.
- One active release-action table in `controls/padmapper.cpp:12–30`.
- One suppressed button and menu-navigation state in `controls/game_controls.cpp:32–34`.
- Static repeaters and right-stick accumulators in `controls/plrctrls.cpp`.
- One control mode, device type, label layout, and cursor.
- Context-free action callbacks in `options.h:791–840`.
- Touch processing that changes global cursor/virtual-pad state before dispatch (`engine/events.cpp:65–69`).

### 5.2 New input boundary

```text
SDL device event
  -> physical registry: instance ID + capabilities + connection generation
  -> seat binding
  -> per-seat axes/buttons/modifiers/repeat state
  -> per-seat UI state or typed action
  -> owning client endpoint and hero
  -> existing production command application
```

Keep device ID, seat ID, client ID, and player slot distinct. Do not infer the hero from SDL enumeration order.

In SDL2, a device-added event supplies an enumeration index. Open that device, then store its instance ID. Button, axis, removal, and remap events use instance IDs. A player index is a separate concept.

Resolve ownership before any motion or button-state mutation. Route both edge events and polled state queries through the same binding.

Prevent raw joystick and mapped game-controller events from producing duplicate actions. Preserve existing per-device trigger/hat state where it is already correct.

On disconnect:

1. Cancel that seat's axes, held actions, repeats, drag state, and pending virtual mouse actions.
2. Do not invoke normal release callbacks blindly. Some release callbacks perform an action.
3. Keep the hero and viewport bound to the same seat.
4. Apply the explicit session pause policy.
5. Reject stale events from the old binding generation.
6. Require explicit reassignment and a neutral input state before resuming.

Names and GUIDs often identify a controller type, not one physical device. They are not sufficient reconnect identity for several identical pads.

Use software cursors per seat. One OS cursor cannot represent four simultaneous inventories.

### 5.3 Wii U input policy

Use the existing SDL backend first. Installed `wiiu-sdl2 2.32.10-2` already initializes VPAD/KPAD and reads GamePad channel zero plus WPAD channels zero through three.

Do not add competing VPAD/KPAD initialization or polling inside game logic. If a backend defect is confirmed, fix the matching SDL backend or add a narrow documented adapter.

The installed mappings include GamePad, Pro Controller, Classic Controller, Wii Remote, and Remote+Nunchuk. Mapping strings do not prove that every control layout supports every game action.

Start with dual-stick controllers. Treat Remote/Nunchuk support as another compatibility tier. The installed WUT headers describe one retail GamePad; four GamePads are not the baseline.

The game sets `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS=0` (`utils/display.cpp:553–554`). Preserve the existing Nintendo button translation. Do not apply A/B swapping twice.

Route GamePad touch only to its owner. Apply that seat's output rectangle, letterbox, and logical transform. If reliable ownership is unavailable, disable touch gameplay in local mode initially. All required actions must remain accessible through physical controls.

**Installed-backend risk:** Disassembly of `WIIU_JoystickGetCount` shows an all-five-slots-occupied path that returns `-1`, rather than five. This was independently inspected, but not reproduced on hardware. Test GamePad plus four Pro Controllers. Do not claim that four-player input always fails. GamePad plus three Pros does not hit that exact boundary.

The installed rumble implementation reports unsupported. Its capability result is zero, and its player-index setter does nothing. Make rumble optional. Public SDL API availability is not backend support.

## 6. Views, composition, HUD, and sound

### 6.1 SDL provides composition, not game cameras

The engine draws an indexed 8-bit image on the CPU. It then converts/uploads the image and presents it (`engine/dx.cpp:47–61,304–323`).

`SDL_RenderCopy` can place separate images in destination rectangles. The first implementation does not need render-target textures: upload each software-rendered view, compose the complete frame, and present once.

Keep SDL calls on the required thread. Set viewport, clip, and scale deliberately. SDL2 target changes and resize operations can restore/reset that state.

`Surface` supports offscreen buffers and subregions. However, `at()` is unchecked and `subregion()` does not validate bounds (`engine/surface.hpp:69,111,173`). Use guard tests and explicit clipping.

### 6.2 Extract presentation from simulation

`DrawView()` combines world rendering, automap, inventory, shops, menus, death overlay, and flasks (`scrollrt.cpp:1315–1459`). `DrawAndBlit()` combines rendering and presentation. Both assume one local view.

Proposed boundaries:

```cpp
// PROPOSED contracts, not current APIs.
ViewLayout CalculateViewLayout(Size output, unsigned seatCount);
void UpdateClientPresentation(ClientContext &client, FrameTime frameTime);
void RenderClientView(const ClientView &view, RenderScratch &scratch,
    const Surface &indexedOutput);
void ConvertViewToRgb(const Surface &indexed, const PaletteState &palette,
    RgbSurface &rgb);
void ComposeViews(std::span<const ViewImage> images,
    const ViewLayout &layout, RgbSurface &output);
```

`ClientView` must reference the production context, not a second simplified game model. Render one complete view at a time initially.

Move semantic work out of drawing. `DrawItemNameLabels()` currently changes `cursPosition` and `pcursitem` from hover and clears a global queue (`qol/itemlabels.cpp:150–214`). No-draw action tests would otherwise skip targeting behavior.

Use the same explicit geometry for rendering and hit testing. Do not maintain approximate independent coordinate transforms.

Inject one frame timestamp and interpolation value. Floating-number expiry, missile interpolation, automap clamping, and derived flask values must not advance once per camera accidentally.

### 6.3 Visibility, palette, and scratch isolation

`ProcessVisionList()` marks locally lit tiles only for `MyPlayer`, while processing visions of active players (`lighting.cpp:437–474`). It changes `dFlags`, transparency, and exploration state. Monsters also use world visibility.

For isolated clients, retain each client's normal visibility state. Do not recalculate one context's simulation visibility using another context's camera. For the shared-world alternative, separate viewer masks from world simulation visibility explicitly.

Each context needs its own palette, blending tables, automap state, and local color effects. `Surface::BlitFrom()` copies palette indices without remapping (`surface.cpp:11–16,26–41`). Therefore:

```text
correct: each indexed view + its palette -> RGB -> shared composite
wrong:   copy four indexed views -> apply one final palette
```

`Lightmap::build()` returns a view of one static buffer (`engine/render/light_render.cpp:21,431–449,513–524`). A later build overwrites it. Reuse scratch only after the previous complete render releases it.

Dirty/cursor state is keyed by `PalSurface->pixels` (`engine/backbuffer_state.cpp:21–45`). Several regions of that surface do not obtain independent dirty state. Use full redraw first.

`RedBack()` uses global width/height in unchecked loops (`control/control_panel.cpp:774–785`). A wrong geometry can tint or overwrite another seat. Include death overlays in boundary tests.

### 6.4 Layout and readability

The existing main panel is 640×128. Side panels are 320×352. Menu geometry assumes 640×480 (`control/control_panel.cpp:241–278`; `control.hpp:38`; `utils/display.cpp:139–145`).

| Output example | Layout | Physical rectangle per player | Consequence |
|---|---|---|---|
| 1280×720 | Two side-by-side | 640×720 | Meets legacy dimensions, but the view is narrow. |
| 1280×720 | Two stacked | 1280×360 | Too short for the legacy layout. |
| 1280×720 | Four-grid | 640×360 | Requires a compact HUD or a scaled/letterboxed logical view. |
| 1920×1080 | Four-grid | 960×540 | Meets baseline dimensions. Actual mode and performance remain unverified. |

These are geometry examples, not verified Wii U output modes. Confirm the actual port's mode.

For three players, use the same equal 2×2 grid and initialize the fourth cell as unused/session status. This is a proposed policy. It keeps layout stable when the player count changes.

**Concrete hazard:** `InitAutomapOnce()` computes `MinimapScale = 25 * (gnScreenHeight / 480)` (`automap.cpp:1556–1577`). At height 360, this becomes zero. `DrawAutomap()` then derives a zero divisor (`:1775–1780`). This is source/arithmetical evidence, not a reproduced crash.

Use a 640×480 logical offscreen view for the first isolation test. Do not call it the final TV layout.

For the final 720p four-grid layout, implement compact per-seat HUD geometry. Keep health, mana, selected spell, and belt feedback accessible. Inventory, character, shop, and quest panels must remain inside their owner's view.

Do not stretch 640×480 to 640×360. An aspect-preserving fit is 480×360 with side bars. A roughly 854×480 logical image fits the cell's aspect ratio, but 12-pixel text becomes about nine physical pixels. Higher logical resolution does not prove readability.

Automate containment, focus paths, clipping, minimum geometry, and nonzero scale checks. Verify comfortable TV reading in the final device smoke test.

### 6.5 Hooks and rendering frequency

`lua::GameDrawComplete()` has no view argument (`scrollrt.cpp:1911`; `lua/lua_event.cpp:99–102`). Define its ownership before calling it in several contexts.

With isolated Lua contexts, call each client's existing hook at its documented client-frame cadence. Do not call another client's hook during composition. A new application-level composed-frame hook, if needed, runs once and uses a distinct API.

With a single shared simulation/Lua state, retain one existing hook invocation per session frame unless a documented per-view hook replaces it. Test that render count cannot repeat gameplay side effects.

### 6.6 One physical audio output

`CalculateSoundPosition()` uses `MyPlayer` as the listener (`engine/sound_position.cpp:8–24`). There is one streaming voice and one active music track (`effects.cpp:40–67`; `engine/sound.cpp:48–57`).

Separate semantic sound events from device playback. Include level, source, action/event identity, and owning seat where relevant.

For shared world events, select the loudest eligible local listener with a stable tie rule, then play once. For replicated contexts, define equivalent event identity so the same action is not played four times. Sample name plus wall-clock time is not a safe deduplication key.

Do not collapse two legitimate attacks that happen to use the same sample. Exclude listeners on other levels. Keep local menu sounds attached to their seat.

Choose one session music rule, such as the primary seat's current area. Use a bounded dialogue queue. These are proposed policies to confirm.

A recording sink can test event count, listener selection, ties, pan, queue bounds, mute, and teardown. It cannot prove audible quality.

## 7. Gameplay, heroes, and persistence

### 7.1 Preserve multiplayer semantics

The recommended client-context model keeps one local owner per context. Preserve owner-only rules inside that context rather than converting every replica into a local authority.

Test damage, friendly fire, XP, durability, death drops, resurrection, item pickup, and item transfer through the real command path. The host is not automatically the owner of every level operation. `isLevelOwnedByLocalClient()` uses the first eligible player on that level (`player.cpp:1996–2015`).

Use one session-wide Diablo/Hellfire mode. `UseMultiplayerQuests()` also depends on `sgGameInitInfo.fullQuests` (`tables/questdat.cpp:59–62`). Test supported quest settings; do not infer full-quest correctness from the multiplayer boolean.

Separate leave-seat, hero disconnect, and end-session actions. Current new-game/exit behavior can mark all players as quitting (`gamemenu.cpp:104–121`). One player's Back action must not end everybody's session unexpectedly.

### 7.2 Hero archive isolation

Bind a distinct writable hero identity to each seat. Reject duplicate archives before changing files. Use disposable saves during development.

The complete read path needs context ownership:

- `pfile_read_player_from_save(saveNum, Player&)` opens the base hero using `saveNum` (`pfile.cpp:785–805`).
- It calls `LoadHeroItems(Player&)`, which still opens `gSaveNumber` (`loadsave.cpp:2401–2416`).
- `LoadHotkeys()` uses `gSaveNumber` and `MyPlayer` (`loadsave.cpp:2339–2379`).
- Load metadata such as `gbValidSaveFile` and `gbIsHellfireSaveGame` is global.

Passing four different `saveNum` values to the outer function is insufficient. Refactor the nested archive context and metadata too.

The write path also selects `*MyPlayer` and `gSaveNumber` (`pfile.cpp:527–543,645–648`). Autosave updates one hero (`:827–840`). Make every write target explicit.

Keep multiplayer hero persistence as the minimum contract. Current multiplayer does not offer the same world save/load menu as single-player. Four calls to `SaveGameData()` do not create one atomic party snapshot; it writes one active world/local owner (`loadsave.cpp:2775–2830`).

If resumable couch sessions are required, design a separate versioned snapshot and recovery protocol. Include roster, worlds/deltas, quests, portals, seeds, and persistent transactions.

Even with hero-only saves, item transfers can cross two hero archives. Serialize writes. Define recovery for interruption between writes so a transfer cannot duplicate an item. Staged files and a session transaction/generation record are possible mechanisms; prove the chosen mechanism with fault injection.

Do not silently overwrite single-player heroes. Current save naming depends on multiplayer mode (`pfile.cpp:80–92`). Decide compatible `multi_*` use versus a new local namespace explicitly.

### 7.3 Vendors, stash, and Lua

Vendors are gameplay state, not just panels. Stock and transaction globals live in `stores.cpp:45–74`. Stock generation depends on `MyPlayer` (`items.cpp:4402–4536`). Network clients deliberately have private town seeds (`multi.cpp:885–886`).

For client contexts, retain per-hero stock/seed and transaction state. A shared stock is a gameplay change. If selected, serialize purchases and reject stale offers.

The stash is one global model with profile-wide storage (`qol/stash.h:20–78`; `pfile.cpp:95–107`; `loadsave.cpp:2420–2435`). Purchases can draw from stash gold (`stores.cpp:2687–2700`).

Do not load the same shared stash into four writable replicas. Either use one authoritative household stash with serialized transactions, or bind separate profiles. Keep cursor/page/selection state per seat in both cases.

`player.self()` and `walk_to()` use the implicit local player in Lua (`lua/modules/player.cpp:125–140`). Isolate Lua state per client or implement explicit context dispatch. Sharing immutable script text is different from sharing mutable VM globals.

Test hooks under the chosen policy. Document unsupported mods rather than allowing silent cross-seat effects.

## 8. Wii U integration and file map

### 8.1 Follow the existing platform pattern

Keep local-session, input ownership, rendering, persistence, and testing code shared. Use `Source/platform/wiiu/` only for real hardware differences.

Switch, Vita, and 3DS demonstrate small adapter targets, platform libraries, compile options, and SDK bridges. They do not supply an existing local split-screen implementation.

Use `add_devilutionx_object_library()`, `target_link_dependencies()`, and the platform subdirectory/link-library lists. The platform integration loop is in `Source/CMakeLists.txt:1202–1206`.

| Location | Change | Ownership |
|---|---|---|
| Proposed `Source/local_session/` | Session descriptor, context lifecycle, scheduler, seat bindings, semantic snapshots/events. | Shared core. |
| `player.*`, world/entity modules, `multi.cpp`, `msg.cpp`, `nthread.cpp` | Move mutable state behind explicit client ownership. Extract startup/tick/load progress. | Shared core. |
| `storm/storm_net.*`, `dvlnet/base.*`, proposed local provider | Context-owned endpoints and queued in-memory transport. | Shared core. |
| `controls/`, `options.h`, gameplay action bindings | Route identity before mutation; per-seat state; typed actor actions. | Shared core. |
| `engine/render/scrollrt.cpp`, `engine/dx.cpp`, surfaces, palette, automap, panels | Separate render/update/present; context geometry; palette conversion; compositor. | Shared core with existing SDL wrappers. |
| `engine/sound_position.cpp`, `effects.cpp`, sound backend | Semantic audio routing, listener policy, one physical output. | Shared core. |
| `pfile.cpp`, `loadsave.cpp`, stores/stash/Lua modules | Explicit hero/profile/context ownership and safe transactions. | Shared core. |
| External Wii U CMake profile | Preserve working SDL2, sound, MPQ, save, bootstrap, and package choices. | Wii U adapter. |
| Proposed `platform/wiiu/input_capabilities.*` | Only proven missing device/touch capability bridges. | Optional Wii U adapter. |
| Proposed `platform/wiiu/display.*` | Output flags and lifecycle integration, if current port needs them. | Optional Wii U adapter. |
| Matching Wii U SDL source | Fix a reproduced backend enumeration or mapping defect at its owner. | SDL backend, not gameplay. |
| `CMake/Tests.cmake`, `test/Fixtures.cmake`, proposed runner | Register tests, assets, and independent target diagnostics. | Shared tests/build. |

These proposed paths are a file map, not files already created.

### 8.2 Installed SDK evidence

The inspected target SDL2 headers report 2.32.10. Package metadata reports `wiiu-sdl2 2.32.10-2`. The actual external port may use a different build.

Installed `SDL_video.h:127–129` defines:

- `SDL_WINDOW_WIIU_GAMEPAD_ONLY`
- `SDL_WINDOW_WIIU_TV_ONLY`
- `SDL_WINDOW_WIIU_PREVENT_SWAP`

Binary inspection of `WIIU_SDL_RenderPresent` shows default copying of the same color buffer to TV and GamePad scan buffers. This supports using the current presenter for an initial composite. It is not a hardware runtime proof or proof of independent GamePad rendering.

Do not introduce a second GX2 presenter merely to draw four TV rectangles. First measure the existing SDL path.

Obtain the actual port commit, SDL package/source, launch environment, output mode, controller mapping, memory availability, and logs before integration. Preserve its existing lifecycle and save permissions.

## 9. Non-UI test strategy

### 9.1 Public test boundaries

Agree these boundaries with the implementer before adding tests. Use one failing behavior test, one production change, and one passing test per vertical slice.

| Proposed boundary | What it proves | What it misses |
|---|---|---|
| Device event -> seat action | Device isolation, modifiers, repeats, cancellation, actor identity. | Real Wii U controller reports. |
| Session start/progress/stop | Real initialization, unique heroes, errors, resource ownership. | Complete gameplay and performance. |
| Submit action + fixed-step progress | Production commands, turn gating, authority, simulation results. | Pixels and speaker output. |
| Read-only semantic snapshot | Stable assertions for heroes, world changes, transactions, and state transitions. | Deliberately excluded fields. |
| Per-client render + composition | Geometry, palette, clipping, local UI, render-order isolation. | TV readability and target driver behavior. |
| Save/reload | Correct archive identity and supported persistent behavior. | Untested power-loss cases. |
| Audio policy + recording sink | Event identity/count, listener policy, queue/lifecycle behavior. | Audible output. |
| Target diagnostic | Device enumeration, resources, real presentation, timing, and lifecycle. | Every controller/environment combination. |

Replace only external boundaries such as time, device reports, audio output, and filesystem faults. Do not mock the combat, inventory, command application, save encoding, or renderer that the test must prove.

### 9.2 Existing foundations and limits

| Existing facility | Useful foundation | Important limit |
|---|---|---|
| `test/ui_test.hpp:58–145` | Many UI-domain functions run without drawing. | One hero and global state. Not multi-seat isolation. |
| `test/panel_state_test.cpp:32–106` | Panel behavior specifications. | Helpers duplicate private handler logic. They do not prove the input route. |
| `test/char_panel_test.cpp:13–18` | Character-panel behavior. | Loopback commands are not pumped; local point spending is not the final stat change. |
| `test/game_menu_test.cpp:10–14,49–60` | Menu state checks. | Real close/save-options is bypassed. New runner must initialize options and cleanup properly. |
| `test/timedemo_test.cpp:33–108` | Real archive/data/Lua/game-loop bootstrap. | One player, forced shareware, sound off, single-player start. |
| `test/scrollrt_test.cpp:13–169` | 13 geometry tests. | One global geometry; not several real views. |
| `test/text_render_integration_test.cpp:379–430` | Real offscreen surface and decoded-pixel comparison. | Not a full split-screen world/HUD test. |
| `test/effects_test.cpp:9–67` | Five sound-position tests. | One listener. |
| `test/pack_test.cpp` | 42 `NetPackTest` cases. | No local multi-context gameplay; some tests need MPQs. |
| Render benchmarks | Existing CLX, dungeon, and light-render kernels. | Not full target frame/memory measurements. |

These counts are source observations, not tests executed during this research.

There is no current `--headless`, `--local-players`, or split-screen scenario CLI. `HeadlessMode` is an internal/test flag. The demo format does not record four controller identities and streams.

`HeadlessMode` skips `DrawAndBlit()`, blitting, presentation, and some palette work. A passing logic run does not prove that one pixel was rendered. Use distinct logic-only, offscreen-render, and target-present modes.

### 9.3 Proposed runner

Add an own-main `local_session_runner` that calls the same production entry points as the game. The name, flags, scenarios, and build option below are **new proposals**.

The runner must:

1. Validate a versioned scenario and a unique run ID.
2. Set disposable save/config paths before loading options or heroes.
3. Initialize real options and required platform services.
4. Load project data and the requested licensed Diablo/Hellfire assets.
5. Start through the production session descriptor, not patched success flags.
6. Drive startup, turns, commands, loading, and simulation with bounded progress.
7. Submit tick-indexed seat actions through the real action route.
8. Assert semantic checkpoints and optionally render offscreen.
9. Save, stop, reopen, and verify through production readers.
10. Clean up contexts, workers/services, surfaces, audio, platform state, and SDL in owned order.

Use a scenario schema with content mode, asset manifest, seed, shop fixture/seed policy, tick rate, hero bindings, actions, checkpoints, rendering mode, and deadlines.

Do not compare raw memory, pointers, struct padding, or PNG file bytes. Define snapshots for player slot, level, position, HP/mana, stats, item identities, gold, quests, objects, and save binding. Keep presentation snapshots separate.

Equal game seeds do not imply equal shop stock. Capture or control town seeds at the boundary for deterministic tests.

Use literal expected outcomes and conservation rules. Differential tests against the one-client path are useful, but an existing multiplayer bug must not become the only correctness oracle.

### 9.4 Required logic scenarios

Run one, two, three, and four seats. Use full Diablo and full Hellfire where rules differ. Required acceptance runs must fail on missing assets or unexpected skips.

| Scenario | Required observable result |
|---|---|
| One-client baseline | New production entry preserves supported single-client behavior. |
| Opposite movement | Two seats move in opposite directions in the same tick. Other heroes remain unchanged. |
| Cross-pad modifier | A modifier on pad A cannot combine with a button on pad B. |
| Held actions/repeats | Releasing A does not stop B. Repeat rates do not depend on processing order. |
| Actor vs destination | Identical actions from two endpoints affect their own heroes, not the destination slot as actor. |
| Panels without drawing | A opens inventory, B uses a shop, C attacks, D changes a spell. Each result has the correct owner. |
| Label targeting without drawing | Production hit testing selects/picks the correct item without a render call. |
| One-item race | Two heroes request one floor item. Exactly one receives it. No floor/save duplicate remains. |
| Concurrent drops | Two different drops retain both identities. No shared transient overwrite occurs. |
| Combat | Verify damage, mana, durability, kill credit, XP, friendly fire, death drops, and resurrection exactly once. |
| Shared encoding on Wii U | Check spawned-monster seeds, both item-flag readers, monster-sync length, and native leave events from §4.6. No validation bypass is allowed. |
| Independent levels | One hero leaves while others continue. Their world/input remains valid. Test four levels, then reunion and deltas. |
| Pause/controller loss | All intended simulations freeze; hardware events continue; resume produces no stale held action or time burst. |
| Reconnection | New instance ID requires valid reassignment; old-generation events cannot control the hero. |
| Hero persistence | Different extended items, hotkeys, gold, stats, and names survive save/reload in the correct archives. |
| Vendor/stash | Transactions conserve item identity and gold under the approved stock/stash policy. |
| Faults | Failed load, disk full, failed rename, corrupt save, allocation failure, and transport pressure return errors without false success. |
| Lifecycle | Repeat 100 start/stop cycles, including partial failures. A later one-client run has no stale four-client state. |
| Context isolation | Alternate A/B operations; save/destroy/recreate A while B remains active. |
| Render independence | Zero, one, or four draw passes do not change authoritative gameplay or RNG. |
| Audio | One replicated event follows the output policy; two legitimate same-sample events remain distinct. |

Use SDL virtual joystick state setters for the SDL-to-router path where supported. A pushed event alone may disagree with `SDL_GameControllerGetButton()` state. Pump SDL after changing the virtual device, then call the production router.

Do not use four separate single-player passes as proof of one shared multiplayer session.

### 9.5 Automated rendering checks

Before manual smoke, run real render code with fixed assets, font, language, palette, options, clock, and interpolation.

- Test one/two/three/four layouts, odd sizes, nonzero origins, and padded pitch.
- Require in-bounds, nonoverlapping rectangles and initialized unused cells.
- Guard neighboring regions and row padding with sentinel bytes.
- Draw sprites, wall overhangs, cursor restore, held items, labels, death tint, panels, and automap at every boundary.
- Verify small-height minimap and compact-HUD geometry through the production path.
- Render A then B, B then A, and A again. Compare each view's decoded pixels.
- Use the same palette index with different colors in two views. Verify RGB output after composition.
- Verify independent fades, visibility, infravision, exploration, targeting, and local menus.
- Check hook cadence, fixed-time label expiry, lightmap lifetime, and one-view regression.
- Run address/undefined-behavior sanitizers on the native build. Keep guards: an in-allocation neighbor overwrite may evade sanitizers.

Do not automatically replace golden images after a failure. Review intentional changes.

### 9.6 Target diagnostics before manual smoke

The Wii U runner can read a scenario from SD and write JSONL plus a final result file. It does not need interactive command-line support.

Use a fresh run ID, maximum tick count, wall-clock deadline, and explicit PASS/FAIL. A stale result file must not pass a new run.

Run logic-only, offscreen, and real-presentation scenarios through the same packaged bootstrap. Include actual save storage, repeated levels, context destruction, and sustained combat.

Run a separate input diagnostic using the production registry/router. Log instance ID, name, GUID, mapping, seat, connection generation, axes, buttons, and add/remove events.

Test GamePad plus one/two/three Pros, and four Pros with GamePad connected. Include equal/opposite simultaneous axes, held buttons, reconnect order, and Home/focus behavior. This requires physical input but no game-menu navigation.

Measure the working one-player port first. Then measure two and four contexts in shared-level and different-level scenarios.

Record total and per-stage median/p95/p99/max time, missed deadlines, input-to-action delay, peak/retained memory, transition peaks, audio resource count, and long-run growth.

A suggested initial target is 30 presented frames/s with normal 20 ticks/s logic. This is a proposal, not measured hardware capability. Agree the actual target and memory safety margin with the port owner.

For scale only: four 640×480 indexed images require 1,228,800 pixel bytes. Four RGBA images require 4,915,200 bytes. A 1280×720 RGBA composite adds 3,686,400 bytes. This excludes worlds, assets, row padding, lightmaps, driver copies, and GPU resources. It is not a target-fit claim.

If budgets fail, optimize or revise the approved architecture before completing UI work. Do not slow game logic silently or hide a player-count reduction.

## 10. Build integration and commands

### 10.1 Existing baseline commands — not executed here

Use the documented native dependencies and a C++23 toolchain. CMake/CTest were unavailable in this research shell.

```sh
cmake -S . -B build-local-baseline -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-local-baseline --parallel 4 --target \
  scrollrt_test effects_test panel_state_test inventory_ui_test \
  spell_ui_test char_panel_test game_menu_test store_transaction_test \
  visual_store_test stash_test pack_test timedemo_test
ctest --test-dir build-local-baseline -N
ctest --test-dir build-local-baseline --output-on-failure \
  -R '^(Scroll_rt|Effects|PanelStateTest|InventoryUITest|SpellUITest|CharPanelTest|GameMenuTest|StoreTransactionTest|VisualStoreTest|StashTest|NetPackTest|Timedemo)\.'
```

For the existing offscreen text-render target:

```sh
cmake -S . -B build-local-baseline -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DUSE_SDL1=OFF -DUSE_SDL3=OFF \
  -DDEVILUTIONX_SCREENSHOT_FORMAT=DEVILUTIONX_SCREENSHOT_FORMAT_PNG
cmake --build build-local-baseline --parallel 4 --target text_render_integration_test
(cd build-local-baseline && ./text_render_integration_test)
```

Provide licensed MPQ assets through the supported paths. CI uses `spawn.mpq` for some tests; this feature's required gates also need full Diablo/Hellfire. Do not commit proprietary archives.

### 10.2 Proposed targets and commands — require implementation

The following options, targets, suites, scenario files, and runner flags do not exist yet.

```sh
cmake -S . -B build-local-coop -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DBUILD_LOCAL_SESSION_RUNNER=ON
cmake --build build-local-coop --parallel 4 --target \
  local_seat_test local_session_test split_view_test local_save_test local_session_runner
ctest --test-dir build-local-coop --output-on-failure \
  -R '^(LocalSeat|LocalSession|SplitView|LocalSave)\.'
./build-local-coop/local_session_runner \
  --scenario test/scenarios/local_coop/four_player_town.json \
  --data-dir /path/to/user-provided-mpq \
  --output-dir /tmp/devilutionx-local-coop-run --render offscreen
```

Suggested runner target skeleton:

```cmake
# PROPOSED. Keep outside the BUILD_TESTING gate.
option(BUILD_LOCAL_SESSION_RUNNER "Build local-session automation" OFF)
if(BUILD_LOCAL_SESSION_RUNNER)
  add_executable(local_session_runner test/local_session_runner.cpp)
  target_link_dependencies(local_session_runner PRIVATE libdevilutionx)
  # Reuse the actual platform bootstrap, link libraries, and packaging.
endif()
```

Register this before `resolve_target_link_dependencies()`. The skeleton is not a complete Wii U packaging recipe.

Most engine tests link `test_main` and the full engine. The runner needs its own `main`, not `test_main`. Use existing standalone-test patterns for pure layout/router units.

Register tests in `CMake/Tests.cmake` and fixtures in `test/Fixtures.cmake`. Use `ctest -N` to verify discovered suite names.

Cross-compilation and `DISABLE_DEMOMODE` disable `BUILD_TESTING` (`CMakeLists.txt:225–231`). Keep the target runner behind a separate option. Do not run a Wii U executable through host GoogleTest discovery.

Reproduce the external port's actual CMake profile and RPX/WUHB packaging. No complete Wii U configure command is supplied because that working port is absent here.

## 11. Implementation sequence and release gates

### 11.1 Ordered work

| Stage | Production change | Required proof before the next stage |
|---|---|---|
| 0. Contract and baseline | Confirm travel, saves/stash, controllers, pause, mods, output, and budgets. Capture current port/toolchain. | Existing one-player behavior and relevant test baseline recorded. |
| 1. One explicit client | Extract context lifecycle, ownership, action/start/tick/save boundaries. Retain normal one-client wrappers. | Same real behavior without UI navigation; failure cleanup and one-client regressions pass. |
| 2. Two endpoints and scheduler | Add context-owned SNet state, in-memory delivery, bounded startup/resync/load, explicit clocks. | Two clients start/join, exchange turns/commands, travel, and shut down without starvation. |
| 3. Two clients, real behavior | Isolate owner state, RNG, Lua, heroes, vendors, item transactions, and persistence. Fix applicable shared encoding defects. | Combat, item races, saves, menus, encoding, and cross-context destruction pass with no drawing. |
| 4. Early Wii U resource gate | Run two and four contexts with scripted logic and representative assets. | Memory/tick/loading limits pass before major HUD work. Otherwise stop for a design decision. |
| 5. Input ownership | Route SDL identity before mutation; per-seat state, cancellation, reconnect, typed actions. | Virtual-device tests and target device diagnostic pass. |
| 6. Offscreen views | Extract presentation update, geometry, render-only path, palette conversion, composition, and audio policy. | Real render/palette/guard/order tests pass; rendering cannot change gameplay. |
| 7. Compact layouts and four seats | Add per-seat HUD/panels, two/three/four layouts, and a lobby adapter to the tested session descriptor. | Logic, lobby/hero assignment, UI state, pixel bounds, and save/reload pass for every player count. |
| 8. Target acceptance | Script actual presentation/audio/storage/lifecycle and sustained scenarios. | Agreed frame/memory/input limits and controller combinations pass on Wii U. |
| 9. Manual UI smoke — last | Use the real lobby, heroes, controllers, TV views, menus, and gameplay. | Human-visible output and interaction match prior automated results. |

Each stage contains small vertical red-green changes. Do not write a large speculative test framework before the first real action works.

### 11.2 Final manual smoke

Run this only after prior gates pass:

1. Start two, three, and four local players through the real UI.
2. Assign distinct controllers and heroes.
3. Check TV readability, layout, labels, and independent cursors.
4. Fight, cast, trade, use shops/stash, die, and resurrect.
5. Travel independently and reunite.
6. Disconnect/reconnect a controller and use pause/resume.
7. Save, exit, restart, and verify each hero.
8. Check actual sound, output mode, and Home/focus lifecycle.

A failed smoke test still blocks release. “Last step” does not mean an optional step or a guarantee that no issue remains.

### 11.3 Completion checklist

- [ ] Product rules and restricted alternatives are explicitly agreed.
- [ ] One process runs one through four real client contexts.
- [ ] All controllers and actions remain attached to the correct hero.
- [ ] Independent travel works, or an explicitly approved restriction is documented.
- [ ] Turn progress, loading, pause, and shutdown cannot starve another context.
- [ ] Combat, XP, items, death, resurrection, vendors, and stash pass behavior tests.
- [ ] Shared command encoding and callback contracts pass on big-endian Wii U without validation bypasses.
- [ ] Each hero's full save data and hotkeys remain isolated; interruption recovery is tested.
- [ ] No-draw logic includes targeting and UI state transitions.
- [ ] Offscreen renderer, palettes, clipping, layout, and hook cadence pass.
- [ ] Single-player and ordinary network behavior have regression coverage.
- [ ] Wii U device, memory, timing, and lifecycle gates pass with recorded evidence.
- [ ] Final manual UI smoke passes for full Diablo and Hellfire.

## 12. Evidence, limits, and source index

### 12.1 Work performed

| Check | Result | Evidence boundary |
|---|---|---|
| Current engine/input/render/save/test/build source review | Complete for the described paths. | Static analysis, not execution of the proposed architecture. |
| High-risk source-chain checks | Confirmed actor/destination distinction, nested save globals, local-owner rules, one active world, minimap arithmetic, and shared encoding risks. | No game crash or multiplayer runtime reproduction. |
| Installed Wii U SDL/WUT headers and binary symbols | Inspected. | Package-specific API/backend evidence, not the external port's exact source or hardware behavior. |
| Input API probe with devkitPPC | Syntax-only compile passed. | No link, device execution, or four-controller runtime test. |
| Desktop SDL offscreen composition probe | Passed. | Four indexed palettes converted to RGB and composed without a window. Not DevilutionX or Wii U. |
| Project configure/build/CTest | Not run. | `cmake` and `ctest` were unavailable; no existing test build was found. |
| Wii U gameplay, controllers, performance, or memory | Not run. | Required implementation gates remain open. |
| Document checks | Source-link ranges, TOC, local links, existing test names, and Markdown checks passed. Original documents remained unchanged. | Documentation checks do not prove the proposed implementation. |
| Production changes | None. | This document is the deliverable. |

The desktop probe used host SDL2 2.32.74. It created four 4×4 indexed surfaces with the same index but distinct colors. It converted each to RGBA, composed four 8×8 regions in a 16×16 output, and checked all 256 pixels.

Temporary research artifacts are under `/tmp/wiiu-splitscreen-handoff-74ce5cdb/`. They are not required to understand this handoff and are not permanent test targets.

The probe ran as:

```sh
clang++ -std=c++17 /tmp/wiiu-splitscreen-handoff-74ce5cdb/sdl_compositor_probe.cpp \
  $(pkg-config --cflags --libs sdl2) \
  -o /tmp/wiiu-splitscreen-handoff-74ce5cdb/sdl_compositor_probe
/tmp/wiiu-splitscreen-handoff-74ce5cdb/sdl_compositor_probe
```

Result: `PASS: four independent indexed palettes compose into four rectangles without a window (desktop SDL primitive only).`

The temporary source may not exist on a colleague's machine. Implement the permanent production compositor tests described above; do not treat this probe as acceptance coverage.

### 12.2 Permanent source anchors

All repository links below use the analyzed commit. Recheck these paths after merging another upstream version.

| Topic | Source |
|---|---|
| Startup, multiplayer initialization | [multi.cpp:516–575](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/multi.cpp#L516-L575), [NetInit](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/multi.cpp#L830-L889) |
| Real game logic and scheduler | [GameLogic](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/diablo.cpp#L1524-L1570), [game_loop](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/diablo.cpp#L3493-L3512) |
| Provider singleton and turn state | [storm_net.cpp](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/storm/storm_net.cpp#L29-L37), [nthread.cpp](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/nthread.cpp#L27-L68) |
| Owner-specific XP | [player.cpp:2407–2459](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/player.cpp#L2407-L2459) |
| Active-world load/unload | [interfac.cpp:311–463](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/interfac.cpp#L311-L463) |
| Event mutation before dispatch | [controls/input.h](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/controls/input.h#L15-L29), [controller aggregation](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/controls/controller.cpp#L77-L93) |
| Current render integration | [DrawView](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/engine/render/scrollrt.cpp#L1315-L1459), [DrawAndBlit](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/engine/render/scrollrt.cpp#L1836-L1928) |
| Indexed image and presenter | [dx.cpp state](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/engine/dx.cpp#L47-L61), [present path](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/engine/dx.cpp#L304-L323) |
| Visibility | [lighting.cpp:437–474](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/lighting.cpp#L437-L474) |
| Render-dependent targeting | [itemlabels.cpp:150–214](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/qol/itemlabels.cpp#L150-L214) |
| Small-height minimap | [scale setup](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/automap.cpp#L1556-L1577), [division](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/automap.cpp#L1775-L1780) |
| Shared serialization risks | [spawn reader](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/msg.cpp#L2607-L2636), [dropped-item validation](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/msg.cpp#L1291-L1320), [packed-item validation](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/pack.cpp#L438-L463), [monster sync](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/sync.cpp#L277-L303) |
| Hero archive ownership | [outer reader](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/pfile.cpp#L785-L805), [nested readers](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/Source/loadsave.cpp#L2339-L2416) |
| Existing no-window image test | [text_render_integration_test.cpp](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/test/text_render_integration_test.cpp#L379-L430) |
| Existing real-engine bootstrap test | [timedemo_test.cpp](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/test/timedemo_test.cpp#L33-L108) |
| Test build and cross-build gate | [Tests.cmake](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/CMake/Tests.cmake#L131-L142), [CMakeLists.txt](https://github.com/diasurgical/DevilutionX/blob/74ce5cdb15da44b28427ad6ee2b107bd6dcd6f61/CMakeLists.txt#L225-L231) |

### 12.3 SDL and installed-platform references

Version-specific public API evidence came from installed SDL2 headers. A Context7 lookup returned SDL3 material despite SDL2 queries; those signatures and semantics were not used.

Useful SDL2 reference pages:

- [SDL_RenderCopy](https://wiki.libsdl.org/SDL2/SDL_RenderCopy): destination-rectangle composition.
- [SDL_RenderPresent](https://wiki.libsdl.org/SDL2/SDL_RenderPresent): complete-frame presentation and thread constraints.
- [SDL_CreateSoftwareRenderer](https://wiki.libsdl.org/SDL2/SDL_CreateSoftwareRenderer): rendering into a surface.
- [SDL_SetRenderTarget](https://wiki.libsdl.org/SDL2/SDL_SetRenderTarget): SDL2 target/state semantics.
- [SDL_RenderSetViewport](https://wiki.libsdl.org/SDL2/SDL_RenderSetViewport): viewport behavior.
- [SDL_GameControllerOpen](https://wiki.libsdl.org/SDL2/SDL_GameControllerOpen): opening by device index.
- [SDL_ControllerDeviceEvent](https://wiki.libsdl.org/SDL2/SDL_ControllerDeviceEvent): index versus instance identity.

Installed target evidence paths:

- `/opt/devkitpro/portlibs/wiiu/include/SDL2/SDL_version.h`
- `/opt/devkitpro/portlibs/wiiu/include/SDL2/SDL_render.h`
- `/opt/devkitpro/portlibs/wiiu/include/SDL2/SDL_events.h`
- `/opt/devkitpro/portlibs/wiiu/include/SDL2/SDL_video.h`
- `/opt/devkitpro/portlibs/wiiu/lib/libSDL2.a`
- `/opt/devkitpro/wut/include/vpad/input.h`
- `/opt/devkitpro/wut/include/padscore/wpad.h`

Binary findings used `powerpc-eabi-nm`, `powerpc-eabi-strings`, and `powerpc-eabi-objdump -dr --disassemble=<symbol>`. Recheck `WIIU_JoystickInit`, `WIIU_JoystickGetCount`, `WIIU_JoystickRumble`, and `WIIU_SDL_RenderPresent` against the exact SDL source used by the port.

### 12.4 Handoff prerequisites

Before implementation starts, provide the colleague with:

1. This document and the actual working Wii U port commit.
2. Its build configuration, SDK/SDL package versions, and packaging commands.
3. Disposable hero saves and legally provided Diablo/Hellfire assets.
4. The approved travel, persistence, stash, pause, controller, and mod policies.
5. A target device with the required controllers and a repeatable result/log collection path.
6. Agreed memory, simulation, display, and input-latency limits.

The next action is to confirm those decisions, then implement the one-client production boundary and the first two-client non-UI test. Do not begin with four camera rectangles.
