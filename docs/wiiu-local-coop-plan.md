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
- Split screen at the port's effective **853x480**: 2 side-by-side views or a
  2x2 grid, rendered at their cell size with per-view geometry, shared panel,
  per-seat name/HP/MP text.
- `Settings -> Gameplay -> Local Co-op Players` selects 1–4 seats.
- 4-seat sessions boot; 2-seat input is verified, 4-pad input is not.

## Phases

### A. Per-seat status HUD
Compact per-view read-only HUD: HP/mana (globes or bars), belt contents, active
spell, XP. Values come from `Players[seat]`; no input yet.

### B. Per-seat quick actions
Belt use (heal/mana), spell selection, automap toggle per seat. Requires
per-seat UI flags instead of the single global `invflag`/`AutomapActive`/etc.

### C. Per-seat panels
Inventory, character sheet, spellbook, quest log and stash inside the owning
seat's view (compact) or as a seat-owned overlay; software cursor per seat and
controller navigation. Mouse stays seat 0. A personal panel must not pause the
other players.

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

- **Seat-aware rules**: XP, item pickup/transfer, quests, vendors, death and
  resurrection still contain `MyPlayer`-centric paths. Audit before calling the
  co-op playable "complete".
- **World save coverage**: confirm the single-player world save contains
  everything a co-op session needs (ground items, deltas, portals, vendor
  stock). Extend if not.
- **720p**: setting `1280x720` in `diablo.ini` presents a white frame on this
  port; stay at the 853x480 window until that presentation bug is fixed.
- **4 seats on hardware**: memory/frame budget and WPAD enumeration are unknown
  until the hardware gate.
