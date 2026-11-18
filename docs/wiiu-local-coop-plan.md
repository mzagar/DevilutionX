# Wii U local co-op — implementation plan (in progress)

Branch `wiiu-splitscreen`. This is the working plan for turning the current
shared-world co-op into a complete local experience on real hardware.

## Goal

One Wii U, **2–4 local players**, each with their own controller, own hero and
own UI (status bar, belt, inventory, character sheet, spellbook, automap) — and
a **save/load that resumes the co-op session exactly where it stopped**.
Real Wii U with real controllers and a proper console UX is the acceptance
target; Cemu remains the fast loop.

## What already works (verified in Cemu)

- `InitLocalCoop(seats)`: one shared world, one hero per seat, single-player
  rules, no turn worker.
- Seat-tagged commands (`SNetSendMessageAs`) and per-seat input ownership
  (`InputSeats`, `SeatInput`); each pad drives its own hero.
- **One complete client interface per seat.** Every seat renders its own
  640x480 Diablo screen (world, main panel, flasks, belt, spell button, control
  buttons, XP bar, inventory/character/spellbook/quest/stash overlays) into an
  indexed canvas; the canvas is then scaled (aspect preserved) into that
  seat's cell and composed into the presented frame. Players see exactly the
  multiplayer client UI they would see on their own TV — not a shared panel.
  `CalculatePanelAreas()` uses the canvas geometry while split screen is
  active, so panel drawing and hit testing share that space.
- Per-seat UI flags: automap, inventory, character, spellbook and quest log are
  toggled per seat and rendered only into the owning seat's canvas. A pad that
  belongs to seat 1+ never drives the shared seat-0 UI.
- `Multi Player -> Local Co-op` lobby: seat count (2-4), a hero per seat (the
  existing hero list, rejects nothing yet), split-screen toggle, Start/Cancel,
  and resume of a saved co-op session. `Multi Player -> Online Multiplayer`
  keeps the original provider dialog.
- Per-seat saves: every seat writes its own `single_N.sv` on level change,
  autosave, exit and cleanup; the world save goes into seat 0's archive once.
- Co-op session descriptor (`local_coop_session.ini`): seat -> save mapping,
  split flag; the lobby offers Resume and `StartGame(loadGame)` restores the
  world.
- 4-seat sessions boot and run in Cemu (2x2 grid). 2-seat input is verified,
  4-pad input is not.

## Phases

### A. Per-seat status HUD — done differently
Instead of a compact HUD the branch went straight to a full client interface
per seat (see above), so there is no separate compact HUD to maintain.

### B. Per-seat quick actions — done
Belt use (heal/mana), spell cast, automap toggle and panel toggles act on the
seat that pressed them (per-seat flags instead of the global
`invflag`/`AutomapActive`/... for seats 1+).

### C. Per-seat panels — partially done
Inventory, character sheet, spellbook and quest log open inside the owning
seat's canvas (full 320x352 side panels, correct art, correct data). Remaining:
- **per-seat pointer/cursor**: only seat 0 has a cursor today, drawn inside its
  own canvas with pointer input mapped from the physical frame. Seats 1+ see
  their panels but cannot yet select inside them with their own pad; pad menu
  navigation still moves the single global cursor.
- stash/vendor interaction per seat.
- mouse stays seat 0 (by design).

### D. Pause / menu policy
Explicit session pause freezes all seats; controller loss handling; the Back
action of one seat must not end the session.

### E. Lobby
`Multi Player -> Local Co-op` (new connection entry, not a top-level menu item):
seats with hero per seat, explicit controller claim ("press A"), split toggle,
Start/Back. Replaces the current Settings/ini flow for normal play. Hero list
rejects duplicates and missing controllers.

### F. Per-seat persistence
Write every seat's hero to its own `single_N.sv` (autosave and exit), not just
`MyPlayer`. Serialize writes; validate a full reload. A party descriptor records
which hero belongs to which seat. Controller instance IDs are **not** saved:
they are re-claimed in the lobby on load.

### G. Co-op session save/load/resume
The user requirement: save a local co-op game and load it back **where it
stopped**. The single-player world save already lives in the hero archive; for
co-op:

- save the shared world **once** together with each seat's hero archive;
- save a party descriptor (seat -> hero save, game mode, split setting);
- load: restore the world (level, quests, portals, ground items, seeds) and all
  seat heroes; the lobby re-assigns controllers; reject incompatible heroes
  (mode/mods/duplicates);
- verify position, level, quests, inventories, belt, stash, vendors and deltas
  after a save -> quit -> relaunch -> load round-trip.

No resumable world for network multiplayer; this is local co-op only.

### H. Real-hardware acceptance
2–4 pads (GamePad + Pro Controllers/WPAD), split rendering, per-seat HUD and
save/resume, HOME/ProcUI, TV/DRC output, memory and frame timing. Expect small
fixes at this gate (mappings, rumble, enumeration).

## Acceptance checklist

- [ ] 2, 3 and 4 players with distinct controllers and heroes.
- [ ] Each seat: movement, attack, belt, spell, inventory, map, character sheet.
- [ ] Personal panels do not pause or block other seats.
- [ ] Each hero writes and reloads its own archive.
- [ ] Co-op save/load resumes world, quests, items, positions and inventories.
- [ ] No duplicate heroes; missing controller blocks Start with a clear reason.
- [ ] 853x480 split (2 and 4 cells) runs on the console at a stable frame rate.
- [ ] Single player and existing network multiplayer unchanged.

## Risks / open issues

- **Per-seat pointer.** Panels render per seat but only seat 0 has a working
  cursor. Each seat needs its own cursor position and its own hit testing in
  canvas coordinates before panel interaction is complete for seats 1+.
- **Seat-aware rules**: XP, item pickup/transfer, quests, vendors, death and
  resurrection still contain `MyPlayer`-centric paths. Audit before calling the
  co-op playable "complete".
- **World save coverage**: confirm the single-player world save contains
  everything a co-op session needs (ground items, deltas, portals, vendor
  stock). Extend if not.
- **720p**: setting `1280x720` in `diablo.ini` presents a white frame on this
  port; stay at the 853x480 window until that presentation bug is fixed.
- **Aspect at 853x480**: each seat is a 640x480 client screen on a 16:9 frame,
  so 2-seat cells letterbox (426x319 inside 426x480) and 4-seat cells letterbox
  to 320x240. Text stays 12px logical but is scaled down; the 4-seat grid is
  legible but small on a TV.
- **4 seats on hardware**: memory/frame budget (4 canvases are reused
  sequentially, ~300 KB of scratch) and WPAD enumeration are unknown until the
  hardware gate.
