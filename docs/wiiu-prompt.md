# Wii U Port — Session Prompt

> **Role of this file:** a copy-paste prompt to start a new session on this port.
> It is a *pointer*, not a copy of the state — the state lives in
> [`wiiu-handoff.md`](wiiu-handoff.md) (where to resume) and
> [`wiiu-progress.md`](wiiu-progress.md) (how it works).

---

Copy everything below into the first message of a new session.

---

Nastavljamo rad na Wii U portu DevilutionX-a u ovom repozitoriju
(branch `wiiu`).

**Prvo pročitaj:**
1. `docs/wiiu-handoff.md` — gdje smo, što je sljedeće, kako se builda i pokreće
2. `docs/wiiu-progress.md` — tehnički detalji, root causes, pitfalls

**Kontekst u jednoj rečenici:** Diablo radi i na **pravom Wii U-u pod Aromom** i
na Cemu 2.6.0 macOS (display, asseti, tipkovnica, text input, zvuk, gameplay,
savegame) iz **istog** self-contained WUHB-a u Aroma layoutu. Artefakt je
**potpuno samostalan**: engine, shareware fallback i **Hellfire mod**
(`assets/mods/hf.mpq`) su unutar WUHB-a; na SD kartici su samo retail MPQ-ovi
(`DIABDAT.MPQ` + `hellfire.mpq`/`hf*.mpq`, `tools/wiiu/extract-gog-mpqs.sh` ih
izvadi iz GOG installera) i ono što igra sama piše u
`sd:/wiiu/apps/devilutionx/`. Pravi hardver je validiran 7. i 8. 11. 2026. i
**sve se dosad ponašalo identično kao na Cemu**; razvoj dalje ide **Cemu-first**
jer je brži, a Wii U služi za potvrdu milestona.

**Tri stvari koje će te ugristi ako ih zaboraviš:**
1. **Cemu `tweq` bug** — Cemu trapira `tweq` i kad uvjet nije ispunjen, a GCC to
   emitira za shared_ptr use-count guard → **svaki copy shared_ptra vješa
   guest**. Simptom: tihi hang, log stane, Cemu na ~200 % CPU. Uvijek pokreni
   `tools/wiiu/cemu-tweq-workaround.py` na `devilutionx.elf` **prije**
   `elf2rpl`, i to nakon **svakog** rebuilda. Za pravi hardver ne treba.
2. **`elf2rpl` traži nestripani ELF** — strip patch se gubi na svakom
   `cmake` re-runu.
3. **Hellfire je Lua *mod*, ne game data.** `gbIsHellfire` postavlja **isključivo**
   `mods/hf/init.lua` (`hellfire.enable()`), a mod loader je `optional=true` →
   ako mod fali, **ne prijavi ništa**. Simptom: MPQ-ovi su tu, `HaveHellfire=1`,
   dijalog *Enter Hellfire* se pojavi — a u izboru klasa su samo 3 klase, bez
   Monka. Kod nas je mod **zapakiran u WUHB** kao `assets/mods/hf.mpq`
   (`tools/wiiu/build-hf-mpq.sh`) i **mora biti packed MPQ**, ne direktorij —
   inače se ignorira `saveExtension=hsv` i Hellfire dijeli `single_*.sv` s
   Diablom.

**Build + run u jednom potezu:**
```bash
tools/wiiu/wiiu-run.sh 40              # build → patch → package → run → log
tools/wiiu/wiiu-run.sh 40 --no-build   # samo re-run
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

**Sljedeći zadatak: dovršiti mrežni multiplayer (feature parity).** Cilj je da
Wii U radi ono što ostale platforme već rade; lokalni split-screen co-op dolazi
poslije (proposal: `docs/wiiu-local-multiplayer.md`).

Stanje: **M0–M3 gotovi** (`NONET=OFF`, `nsysnet` + asio + libsodium rade; asio je
pod Cemuom napravio pravi HTTP round-trip, dakle Cemu bridža `nsysnet` na mrežu
hosta). **M4 blokira bug u text inputu**: u *Create Game* polje za password ne
prima tipke, isto kao ni polje za adresu u *Client-Server (TCP)*.

Red rada:
1. **Popraviti text input** u multiplayer dijalozima (`keyboard.cpp` zaobilazi
   swkbd za ime lika — treba provjeriti `SDL_TEXTINPUT` za ostala polja)
2. **Testirati bez usera**: 2 Cemu instance (različiti SD/MLC) preko
   `127.0.0.1:6112`, ili native macOS DevilutionX build kao drugi peer
3. **ZeroTier** — provjeriti izvedivost (`zerotier_lwip` vs `zerotier_native` na
   devkitPPC) prije nego se obeća; `DISABLE_ZERO_TIER ON` ga trenutno skriva
4. M5: UX za upis adrese (konzola bez tipkovnice)

Za UI automatizaciju (klikanje/tipkanje iz skripte) treba **Accessibility
dozvola** za terminal u kojem se vrti pi; dozvola je već dana, ali **iTerm se
mora restartati** da je preuzme. Detalji u `docs/wiiu-handoff.md` → "Next task".

**Čišćenje branche dolazi poslije featura** (throwaway `tweq` test blokovi u
`Source/engine/sound.cpp`, per-file `SFX_DBG` spam u `Source/effects.cpp`,
`Source/platform/wiiu/stub.cpp`, CRLF churn u top-level `CMakeLists.txt`,
redundantni `-DLUA_USE_C89` u `CMake/platforms/wiiu.cmake`) — **ne prije**, jer
debug output nam treba za razvoj.

**Pravila rada:**
- Debug output (OSReport `DBG`/`SND_DBG`/`SMPL_DBG`/`SFX_DBG`) **ostaje u treeu**
  — to nam je glavni alat za debugiranje. Na Wii U-u (`init.cpp`) se zove `Diag()`
  i osim u OSReport piše i u `boot-diag.txt` na SD kartici
- OSReport printf **ne podržava** `%zu`/`%lu` — koristi `%d` s castovima
- Dio fajlova je CRLF u gitu (`.gitattributes` ima `* -text`) — ne daj editoru
  da prepiše line endings
- Cemu `log.txt` se **prepisuje** svaki run, ne append-a se
