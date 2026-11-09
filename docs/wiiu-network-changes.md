# Wii U network multiplayer — change set

**Purpose:** This file describes the network multiplayer change set on branch
`wiiu`. It gives the reason for each changed file. The change set is committed
as one commit on `wiiu`.

For status, read [`wiiu-handoff.md`](wiiu-handoff.md). For findings, read
[`wiiu-progress.md`](wiiu-progress.md), findings 16–20.

Terms: **LE** = little-endian, **BE** = big-endian, **RNG** = random number
generator, **WUHB** = the packaged Wii U game.

## 1. Branch state

The network work is committed as one commit on `wiiu`. Its parent commit:

```
b62aad3aa docs: network multiplayer parity status, the text-input blocker and how to test it
3bc496f96 Wii U: enable network multiplayer (TCP) for feature parity
2e45540d0 docs: analyse local co-op and network multiplayer for the Wii U port
0a4e8834a docs: record the Hellfire mod, save namespaces and the final WUHB
a06e7c04d tools/wiiu: pack the Hellfire mod into the WUHB as assets/mods/hf.mpq
1bdb43685 docs: record full game + Hellfire support
507cfab24 Wii U: log which game data was loaded after LoadGameArchives
2ddea4b82 tools/wiiu: script to extract the retail MPQs from the GOG installer
```

The change set has 22 modified files and 9 new files.

## 2. Result

- The wire format now works for 32-bit BE ↔ 64-bit LE peers.
- The TCP provider keeps write order, delivers leaves, and keeps the host port.
- The console has an RNG backend for libsodium. The first join used to abort.
- Tests: 21/21 wire tests pass. The desktop matrix passes. Wii U ↔ desktop
  sessions pass. The real game works with a macOS host and a Wii U client.

## 3. Changed files and reasons

### 3.1 Shared wire format

| File | Reason |
|---|---|
| `Source/dvlnet/spawned_monster_delta.hpp` (new) | Defines the spawned-monster delta on the wire. The old code copied a C struct with `sizeof`. That is 16 bytes on 64-bit LE and 12 bytes on 32-bit BE, so late join and resync read wrong bytes. The new format is 16 bytes and matches 64-bit LE peers. |
| `Source/msg.cpp` | Uses the new codec. Buffer sizes use `SpawnedMonsterWireRecordSize`. `OnSpawnMonster` decodes the LE seed. |
| `Source/pack.cpp` | `UnPackNetItem` used `Swap16LE` on the 32-bit `dwBuff`. On BE this loses `CF_HELLFIRE`. On LE it drops the upper half, which is harmless today because all used `CF_*` flags are below bit 15. The code now uses `Swap32LE`. |
| `Source/sync.cpp` | The debug assert used the raw `header.wLen`. The raw value is wrong on BE. |
| `Source/dvlnet/base.cpp` | `_SNETEVENT` data must be LE. The code passed a native enum. The producer now converts. `multi.cpp` converts back. |
| `Source/utils/endian_{read,write}.hpp` | Add `LoadLE64` and `WriteLE64` for the codec. |

### 3.2 TCP provider

| File | Reason |
|---|---|
| `Source/dvlnet/tcp_server.{h,cpp}` | Adds a send queue per connection. asio does not allow two `async_write` calls on one socket at the same time. The server sends from several players to one socket. Also sets up the acceptor with error codes, because every asio error is fatal here. `LocalhostSelf()` now returns `host:port`; before, a host on a port other than 6112 failed the self-connect. It asks the socket only for port 0, because Cemu returns a bad `socklen` from `getsockname`. |
| `Source/dvlnet/tcp_client.{h,cpp}` | Adds the same send queue. Echo replies come from the receive handler. `SNetLeaveGame` waits for the queue to empty before it closes the socket, so peers get `LEAVE_EXIT` and not `LEAVE_DROP`. |

### 3.3 Wii U platform

| File | Reason |
|---|---|
| `Source/platform/wiiu/random.{hpp,cpp}` (new) | libsodium backend. The console has no random device and no `getentropy` or `arc4random`. The default backend then calls `sodium_misuse()`, which is `OSFatal` on the first join. The code uses `/dev/urandom` if it exists, otherwise a ChaCha20 RNG. The seed comes from the OS clock, `OSGetTick`, `OSGetOSID`, clock speeds, addresses, stack bytes, and timer jitter. |
| `Source/platform/wiiu/CMakeLists.txt` | Builds `random.cpp` when packet encryption is on. |
| `Source/main.cpp` | Installs the RNG before `wiiu_init()` and before any libsodium call. |
| `Source/platform/wiiu/init.cpp` | Adds `random: <source>` to `boot-diag.txt`. Also holds the earlier boot probe. |

### 3.4 Tests, tools, and build

| File | Reason |
|---|---|
| `CMakeLists.txt` | Adds `BUILD_NETWORK_HARNESS`. It is independent of `BUILD_TESTING`, because cross-builds force that off. |
| `CMake/NetworkHarness.cmake` (new) | Defines `network_peer`. The target links the real `libdevilutionx`, so the test uses the same code as the game. |
| `CMake/Tests.cmake` | Registers `dvlnet_wire_test`. |
| `test/dvlnet_wire_test.cpp` (new) | 21 tests: golden packet and frame vectors, all packet types, bad input, frame split and join, encryption, wrong password, changed ciphertext, spawned-monster codec. |
| `test/network_peer.cpp` (new) | The no-UI peer. It calls the real SNet create, join, message, turn, and leave functions. It reads `network_peer.ini`, so it runs on a console. It writes JSONL results. It has a `probe = socket` mode for raw socket tests. |
| `test/run_network_scenario.py` (new) | Starts a host and N-1 clients. Checks each result. Scenarios: public, private, wrong password, drop. |
| `tools/wiiu/package-network-peer.sh` (new) | Builds the harness WUHB: tweq patch, `elf2rpl`, `wuhbtool`. |
| `tools/wiiu/cemu-run.sh` (new) | Starts Cemu with SIGPIPE ignored. Cemu 2.6.0 stops with signal 13 on the first multiplayer packet. |

### 3.5 Earlier session changes (also uncommitted)

| File | Reason |
|---|---|
| `Source/platform/wiiu/keyboard.{cpp,hpp}` | Uses the PC text path only. No system keyboard. |
| `Source/DiabloUI/multi/selgame.cpp` | Accepts an empty join password. A console without a keyboard needs this. |
| `Source/diablo.cpp`, `Source/menu.cpp` | The file `skip-intro` on the SD card skips the cinematics. |
| `Source/platform/wiiu/init.cpp` | The boot probe also connects to `127.0.0.1:6112` and sends 12 bytes. |

### 3.6 Documentation

`docs/wiiu-progress.md` adds findings 16–20, the status table, and the session
history. `docs/wiiu-handoff.md` updates the milestones, the run instructions,
the Cemu facts, and the file map. `docs/wiiu-network-changes.md` is this file.

## 4. Scope: are these changes Wii U-only?

No. The shared files have three types of change. Only the last group is for the
console only.

### 4.1 Real shared defects (they occur without a Wii U)

| Change | Effect without a Wii U |
|---|---|
| TCP send queue | We reproduced the defect with two desktop builds. With four players, a peer stopped receiving. After the fix, the same tests pass many times. |
| `SNetLeaveGame` queue flush | The old code could close the socket before the leave packet left. Peers then got `LEAVE_DROP` instead of `LEAVE_EXIT`. |
| `LocalhostSelf()` port | A host on a port other than 6112 failed on all platforms. The listener used `Network.port`, the self-connect used 6112. The desktop test on port 6113 checks this. |
| Spawned-monster wire format | A 32-bit build and a 64-bit build already failed in this delta path. The byte order is a second problem, and only a BE peer hits it. The new format keeps 64-bit LE peers compatible. |

### 4.2 Shared code with a big-endian effect

| Change | Effect on LE |
|---|---|
| `pack.cpp` `dwBuff` width | All used `CF_*` flags are below bit 15, so the old code is harmless on LE today. It fails on BE. It becomes a bug on LE if a new flag uses a higher bit. |
| `base.cpp` leave reason | On LE, native order and LE order are the same. Only a BE peer sees the double conversion. |
| `sync.cpp` assert | The raw `wLen` is byte-swapped only on BE, so the assert fires only on a BE debug build. |

These are still wrong in the shared code. The fix is in the shared code.
Without a big-endian peer, nobody would notice them.

The Wii U is **not** the only big-endian target in this repository:

- **macOS 10.4 Tiger on PowerPC** (`CMake/platforms/macos_tiger.cmake`) keeps
  networking on and only disables ZeroTier. A PowerPC Mac (G3/G4/G5) is
  big-endian, so it is a possible big-endian multiplayer peer already.
- **Amiga / m68k** (`CMake/platforms/amiga.cmake`, plus the `WARPOS`
  PowerPC variant) is big-endian, but the profile sets `NONET ON`, so it has no
  multiplayer.

The Wii U is the only big-endian target where multiplayer is both enabled and
tested.

### 4.3 Cemu effects

| Change | Note |
|---|---|
| `local_endpoint()` fallback | Cemu reports `getsockname` as OK, but returns a bad `socklen`. asio then throws, and the error is fatal here. The code asks the socket only when the port is unknown. |
| Explicit acceptor setup | General robustness. On the console, every asio error is fatal, so each step is checked. |

### 4.4 Console only

- `Source/platform/wiiu/random.{hpp,cpp}` and the RNG hook in `Source/main.cpp`.
- `Source/platform/wiiu/CMakeLists.txt` and the `boot-diag` line.
- The keyboard, `selgame`, and `skip-intro` changes from the earlier session.

The harness and the tools are generic. The desktop matrix found the four-player
defect.

**Why the shared files had to change:** The shared code worked on the common
path. That path is two players, 64-bit LE, and port 6112. The console adds a
second ABI and endianness, and a four-player setup. These conditions exposed the
hidden defects. No change adds a console-only gameplay branch or a new protocol.

## 5. Suggested commit split

1. **`shared: make the multiplayer wire format ABI-independent`** —
   `spawned_monster_delta.hpp`, `msg.cpp`, `pack.cpp`, `sync.cpp`, `base.cpp`,
   `endian_read.hpp`, `endian_write.hpp`, `test/dvlnet_wire_test.cpp`,
   `CMake/Tests.cmake`.
2. **`dvlnet: serialise socket writes and fix the self-connect address`** —
   `tcp_server.{h,cpp}`, `tcp_client.{h,cpp}`.
3. **`wiiu: add a libsodium RNG backend`** —
   `platform/wiiu/random.{hpp,cpp}`, `platform/wiiu/CMakeLists.txt`,
   `main.cpp`, `platform/wiiu/init.cpp`.
4. **`test: add the no-UI network peer`** —
   `test/network_peer.cpp`, `test/run_network_scenario.py`,
   `CMake/NetworkHarness.cmake`, `CMakeLists.txt`,
   `tools/wiiu/package-network-peer.sh`, `tools/wiiu/cemu-run.sh`.
5. **`wiiu: text input, empty join password, skip-intro marker`** —
   `keyboard.{cpp,hpp}`, `selgame.cpp`, `diablo.cpp`, `menu.cpp`.
6. **`docs: record the Wii U network multiplayer status`** —
   `wiiu-progress.md`, `wiiu-handoff.md`, `wiiu-network-changes.md`.

## 6. How to reproduce

```bash
# wire tests
cmake --build build-net --target dvlnet_wire_test && ./build-net/dvlnet_wire_test

# desktop scenarios: 4 players, private, custom port, drop, wrong password
python3 test/run_network_scenario.py --peer build-net/network_peer --players 4 --rounds 512
python3 test/run_network_scenario.py --peer build-net/network_peer --scenario private
python3 test/run_network_scenario.py --peer build-net/network_peer --port 6113
python3 test/run_network_scenario.py --peer build-net/network_peer --scenario drop --drop-after=16
python3 test/run_network_scenario.py --peer build-net/network_peer --scenario wrong-password

# console harness in Cemu (network_peer.ini: role = join)
make -C build-wiiu-net network_peer -j8
tools/wiiu/package-network-peer.sh
tools/wiiu/cemu-run.sh build-wiiu-net/network_peer.wuhb

# real game: host on the Mac, join in Cemu (menus on both sides)
open -n build-macos/devilutionx.app                         # Multiplayer → TCP → Host Game
tools/wiiu/cemu-run.sh build-wiiu-release/devilutionx.wuhb  # Join Game → 127.0.0.1 → empty password
```

## 7. Known gaps

- The Wii U as host is not verified. Cemu stops on the self-connect of the
  guest. A real console is necessary.
- There is no no-UI engine start. The harness tests the transport, not
  `NetInit`, the delta, or gameplay commands.
- The multiplayer menu is not finished. The address comes from
  `Previous Host`. Text input on the console needs a USB keyboard.
- The deferred cleanup in `wiiu-handoff.md` is not part of this change set.
