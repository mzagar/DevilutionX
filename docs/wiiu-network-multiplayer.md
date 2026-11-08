# Wii U — Network Multiplayer (TCP / ZeroTier)

> **Status:** proposal, not implemented. Nothing in this document has been built or tested.
> Companion to [`wiiu-local-multiplayer.md`](wiiu-local-multiplayer.md) (same problem, different scope).

## TOC

1. [Executive summary](#1-executive-summary)
2. [Why the menu only offers "Offline"](#2-why-the-menu-only-offers-offline)
3. [What already exists upstream](#3-what-already-exists-upstream)
4. [What the Wii U port is missing](#4-what-the-wii-u-port-is-missing)
5. [Options](#5-options)
6. [Recommendation](#6-recommendation)
7. [Risks](#7-risks)
8. [Milestones](#8-milestones)
9. [Facts table](#9-facts-table)

---

## 1. Executive summary

**Network multiplayer is not a feature that needs writing — it is a feature that needs
*un-switching*.** The multiplayer stack is present, compiled away by a single CMake flag
(`NONET ON`), and two other consoles (3DS, Switch) have already solved the platform side of
exactly this problem. The work is plumbing, not protocol.

```
NOW:     NONET=ON  ->  loopback  ->  "Offline"       (1 player, no network code compiled in)
GOAL:    NONET=OFF ->  tcp_client ->  "Client-Server (TCP)"  (up to 4 players, LAN/internet)
```

**One reality check up front:** "classic Battle.net" no longer exists in DevilutionX. The
original Battle.net client code was removed upstream; only a `(for bnet? not implemented)`
comment survives. What the project calls multiplayer today is either **ZeroTier** (a virtual
LAN service the players all join) or **Direct TCP** (host/join by IP:port). If the goal is
"play Diablo with a friend over the internet like in 1997", **TCP + a port forward or a VPN
gives the same experience** and is by far the shortest path.

| | Recommendation |
|---|---|
| Transport | **TCP only.** Skip ZeroTier. |
| Encryption | **Bundled libsodium**, unless it fights the toolchain — then `PACKET_ENCRYPTION OFF`. |
| Menu | Existing `Client-Server (TCP)` entry reappears as-is. No new UI. |
| Effort | **M** — ~2 platform files + CMake + a possible sodium build fix. |
| Testable on Cemu | Probably yes (Cemu emulates `nsysnet`) — must be verified first. |

---

## 2. Why the menu only offers "Offline"

`Multi Player` → connection list shows exactly one entry. That is not a Wii U bug — it is a
build flag with an unfortunate label.

```
CMake/platforms/wiiu.cmake:5      set(NONET ON)
        |
        v
CMakeLists.txt:114  DISABLE_TCP        = ON    (forced)
CMakeLists.txt:115  DISABLE_ZERO_TIER  = ON    (forced)
CMakeLists.txt:242  PACKET_ENCRYPTION  = OFF   (forced)
        |
        v
Source/DiabloUI/multi/selconn.cpp:56   #ifndef NONET
                                         ^^^^^^ everything inside is skipped
                                         --> ZeroTier + TCP entries never pushed
                                         --> only the unconditional loopback entry at :64 remains
```

**"Offline" is the name of the provider `SELCONN_LOOPBACK`**, not a statement that
multiplayer is unavailable. `SelconnFocus()` (`selconn.cpp:133-135`) even describes it as
*"Play by yourself with no network exposure."* with `Players Supported: 1`.

And `MakeNet()` ignores the selected provider entirely while `NONET` is defined:

```c
// Source/dvlnet/abstract_net.cpp:21
#ifdef NONET
    return std::make_unique<loopback>();      // <- always, whatever the menu says
#else
    switch (provider) { case SELCONN_TCP: ...  case SELCONN_ZT: ... }
#endif
```

---

## 3. What already exists upstream

### 3.1 The provider stack

```
                        SNetInitializeProvider(provider)
                                     |
                                     v
                        abstract_net::MakeNet()   [abstract_net.cpp:21]
                                     |
              +----------------------+----------------------+
              |                      |                      |
              v                      v                      v
         loopback             tcp_client              protocol_zt
      (single player)      (client+server)        (ZeroTier transport)
              |                      |                      |
              +----------------------+----------------------+
                                     v
                        Source/multi.cpp, msg.cpp, nthread.cpp
                        (players, commands, lockstep) -- UNTOUCHED
```

Everything above `MakeNet` is platform-agnostic and already compiled into our build (the
loopback path proves it). Switching transport is a **one-line dispatch**.

| Provider | File | What it does |
|---|---|---|
| `loopback` | `Source/dvlnet/loopback.cpp` | Pseudo-multiplayer for single player. Sends to self, no-op turns. |
| `tcp_client` | `Source/dvlnet/tcp_client.cpp` | Direct IP connect. **Host and join are the same class.** |
| `protocol_zt` | `Source/dvlnet/protocol_zt.cpp` | ZeroTier virtual LAN. Needs `3rdParty/libzt` + LWIP. |
| `tcp_server` | `Source/dvlnet/tcp_server.cpp` | Embedded server, owned by `tcp_client`. No separate binary. |

### 3.2 TCP is host-and-join in one object

```
tcp_client::create(addrstr)  [tcp_client.cpp:27]   HOST
    local_server = make_unique<tcp_server>(ioc, addrstr, port, pktfty);
    return join(local_server->LocalhostSelf());     // host joins its own server

tcp_client::join(addrstr)    [tcp_client.cpp:34]   JOIN
    parse "hostname:port"   (default port 6112, also Source/options.cpp:952)
    asio::connect(...)
```

So a Wii U player can host, and a PC player (or a second Wii U) can join — **asymmetry-free**,
no dedicated server needed.

### 3.3 Battle.net does not exist

```
$ grep -ri "battle.net|bnet|pvpgn" Source/
Source/dvlnet/base.cpp:475   EVENT_TYPE_PLAYER_MESSAGE (for bnet? not implemented)
Source/DiabloUI/credits_lines.cpp:31   N_("Battle.net Programming")   <- a credit string only
```

The original Battle.net client was removed from DevilutionX upstream. There is no gateway,
no chat server, no channel list to re-enable. Period. The modern substitutes in the
connection menu are ZeroTier and TCP.

---

## 4. What the Wii U port is missing

### 4.1 The four gaps

```
+-------------------------------------------------------------+
| 1. BUILD FLAG     NONET ON -> OFF             [wiiu.cmake:5]|
| 2. SOCKET INIT    socket_lib_init()/finish()  (not called)  |
| 3. ASIO SHIMS     POSIX bits wut lacks        (not present) |
| 4. ENCRYPTION     libsodium for Wii U         (never built) |
+-------------------------------------------------------------+
```

### 4.2 Console precedent — this has been done twice already

| | 3DS | Switch | Vita | **Wii U today** |
|---|---|---|---|---|
| `NONET` | off | off | off | **ON** |
| `DISABLE_ZERO_TIER` | off | ON | — | ON |
| Socket init | `socInit(buf, 1 MB)` `[ctr/sockets.cpp:59]` | `socketInitializeDefault()` `[switch/network.cpp:11]` | `vita_enable_network()` | **missing** |
| WiFi wait | 5 s poll `[ctr/sockets.cpp:17]` | — | — | n/a |
| asio shims | 12 files `[ctr/asio/]` | 8 files `[switch/asio/]` | — | **missing** |
| RNG for sodium | `PS_GenerateRandomBytes` | `csrngGetRandomBytes` | `randombytes_vitarandom_init` | **missing** |
| Hook | `ctr_sys_init()` | `main.cpp` `__SWITCH__` | `main.cpp` `__vita__` | `main.cpp:42` `wiiu_init()` exists |

The Wii U equivalent of the socket init **ships with `wut` already**:

```
/opt/devkitpro/wut/include/nsysnet/_socket.h:45   socket_lib_init();
/opt/devkitpro/wut/include/nsysnet/_socket.h:48   socket_lib_finish();
/opt/devkitpro/wut/include/sys/socket.h           present
/opt/devkitpro/wut/include/sys/select.h           present
/opt/devkitpro/wut/include/sys/ioctl.h            present
/opt/devkitpro/wut/include/netdb.h                present
```

wut's header set is closer to Switch's (which needed only 8 shims) than to 3DS's (12).

---

## 5. Options

### 5.1 Transport

| | Option | Effort | Verdict |
|---|---|---|---|
| **T1** | **TCP only** — `NONET OFF`, `DISABLE_ZERO_TIER` stays ON, link `asio` | **M** | **Recommended** |
| T2 | TCP + ZeroTier — also `3rdParty/libzt` + LWIP + native socket glue | XL | Skip. libzt on Wii U is a project of its own, and its value is "no port forward", which any VPN already gives you. |
| T3 | Keep `NONET ON`, no network | — | Status quo. |

### 5.2 Packet encryption

`PACKET_ENCRYPTION` is `ON` by default whenever `NONET` is off (`CMakeLists.txt:36`, and the
re-evaluation at `:237-242` only force-disables it while `NONET` is still on).

| | Option | Effort | Notes |
|---|---|---|---|
| **E1** | **Bundled libsodium** (`DEVILUTIONX_SYSTEM_LIBSODIUM OFF` is *already* set at `wiiu.cmake:13`) | S–M | `3rdParty/libsodium` is robinlinden's CMake wrapper; it already builds for 3DS/Switch/Vita, so a Wii U (devkitPPC + wut) build is plausible. **Must be probed.** |
| E2 | `set(PACKET_ENCRYPTION OFF)` | S | Works (DOS port does this) but sends the protocol in the clear. Fine over a trusted LAN/VPN, not over the open internet. |

### 5.3 Random source for sodium

Sodium needs `randombytes`. Every console port installs its own implementation
(`switch/random.cpp`, `ctr/random.cpp`).

| | Option | Effort |
|---|---|---|
| R1 | First try sodium's built-in default (labelled `sysrandom`) and see if wut backs it | S |
| R2 | Write `wiiu/random.cpp` against a wut entropy source, mirroring `switch/random.cpp` | M |
| R3 | Skip sodium altogether → use 5.2/E2 | S |

### 5.4 Player-facing UX

| | Option | Effort |
|---|---|---|
| **U1** | **Nothing to build.** Removing `NONET` makes `Client-Server (TCP)` reappear in the existing list, with its existing description and `Players Supported: 4` | 0 |
| U2 | Pre-fill the host address field sensibly (Wii U has no keyboard; `Source/platform/wiiu/keyboard.cpp` exists for text entry) | S |
| U3 | Add a "Network" page for port/host defaults | M — premature |

---

## 6. Recommendation

**T1 + E1 + R1 + U1.** Concretely:

```
CMake/platforms/wiiu.cmake
    -   set(NONET ON)                 # <- delete
        set(DISABLE_ZERO_TIER ON)     # keep
        set(DEVILUTIONX_SYSTEM_LIBSODIUM OFF)   # keep (bundled)

Source/platform/wiiu/network.cpp      (new)  socket_lib_init()/socket_lib_finish()
Source/platform/wiiu/asio/…           (new)  POSIX shims as asio needs them
Source/platform/wiiu/CMakeLists.txt   if(NOT NONET) -> link asio [+ sodium]
Source/main.cpp:42                    wiiu_init() already called; socket init goes inside it
```

**Why this and not more:** the protocol, the server, the lobby, the command layer and the
lockstep thread all already work and are shared with the desktop build. Our only job is to
give asio a working socket underneath it — which is precisely the job 3DS and Switch already
did, in public, in the same repository, with the same toolchain family.

**Why not ZeroTier:** it is the single largest dependency in the tree and it exists only to
avoid port forwarding. A VPN or a forwarded port achieves the same thing with zero code.

**Why encryption stays on if it can:** turning it off is a real (if small) downgrade, and the
"turn it off" switch is always available as a fallback (`E2`), so there is no reason to reach
for it before trying.

---

## 7. Risks

| # | Risk | Impact | Mitigation |
|---|---|---|---|
| 1 | **libsodium may not build for devkitPPC** (`3rdParty/libsodium`, CMake from `robinlinden/libsodium-cmake`) | Blocks E1 only | Fall back to `PACKET_ENCRYPTION OFF` (E2) — one line, already proven by the DOS port |
| 2 | **Cemu's `nsysnet` emulation may not be usable** | No fast test loop; every probe needs real hardware | Probe early (M0) before writing anything |
| 3 | **Asio versions / C++ standard**: asio is a vendored fork (`3rdParty/asio`, tag `112f0118`) | Unknown until tried | Follow Switch's shim list; add only what the compiler actually reports missing |
| 4 | **`socket_lib_init()` needs the network to be up**; Wii U may need a connection check first | Silent connect failures | Log init result; mirror 3DS's WiFi wait if needed |
| 5 | **Turning `NONET` off changes other option defaults** (`DEVILUTIONX_DISABLE_RTTI`, exceptions at `CMakeLists.txt:158-159`) | Larger binary, possible codegen changes | Expected — but re-verify the tweq patch and WUHB size afterwards |
| 6 | **Two different multiplayers now compete for attention** (this doc vs local co-op) | Scope confusion | They are independent: local co-op reuses `loopback`, network uses `tcp_client`. Neither blocks the other. |
| 7 | **Save namespaces collide** — network games use `multi_*`, local co-op would too | Character confusion | Already separate from `single_*`; fine. |

---

## 8. Milestones

```
M0  feasibility probe      NONET=OFF on a scratch build dir; does it compile at all?
                           Does Cemu's network work?  (gate for everything below)
M1  socket init            network.cpp + hook, log result, still no menu change
M2  asio shims             add shims until asio compiles; connect to a PC host
M3  encryption             libsodium build probe; else PACKET_ENCRYPTION OFF
M4  live test              Wii U hosts, PC joins (and vice versa) — Cemu first if possible
M5  polish                 address-entry UX for a controller-only device
```

M0–M3 are all Cemu-side (no SD round-trip). M4 is the first milestone that needs the console.

---

## 9. Facts table

| Fact | Value | Source |
|---|---|---|
| Provider dispatch | one function | `Source/dvlnet/abstract_net.cpp:21` |
| Nonet forces loopback | yes | `abstract_net.cpp:23-24` |
| Connection menu entries | ZeroTier, Client-Server (TCP), Offline | `Source/DiabloUI/multi/selconn.cpp:28` |
| Entries gated by | `#ifndef NONET` | `selconn.cpp:56` |
| Loopback entry | unconditional | `selconn.cpp:64` |
| Loopback description | "Play by yourself with no network exposure." | `selconn.cpp:133-135` |
| Max players | 4 (`MAX_PLRS`) | `Source/multi.h:26` |
| Default port | 6112 | `Source/dvlnet/tcp_client.cpp:39`, `Source/options.cpp:952` |
| TCP host path | `tcp_client::create()` | `tcp_client.cpp:27` |
| TCP join path | `tcp_client::join()` | `tcp_client.cpp:34` |
| Embedded server | yes, no separate binary | `tcp_client.cpp:30` |
| Wii U `NONET` | ON | `CMake/platforms/wiiu.cmake:5` |
| Wii U `DISABLE_ZERO_TIER` | ON | `CMake/platforms/wiiu.cmake:14` |
| Wii U bundled sodium | already configured | `CMake/platforms/wiiu.cmake:13` |
| Switch `NONET` | off (default) | `CMake/platforms/switch.cmake` |
| Switch shims | 8 files | `Source/platform/switch/asio/` |
| 3DS shims | 12 files | `Source/platform/ctr/asio/` |
| Wii U socket API | `socket_lib_init()`/`socket_lib_finish()` | `/opt/devkitpro/wut/include/nsysnet/_socket.h:45,48` |
| Battle.net | removed upstream | `Source/dvlnet/base.cpp:475` |
| asio source | diasurgical fork, tag `112f0118` | `3rdParty/asio/CMakeLists.txt:5` |
| libsodium source | `robinlinden/libsodium-cmake`, tag `a8ac450` | `3rdParty/libsodium/CMakeLists.txt:15-16` |
