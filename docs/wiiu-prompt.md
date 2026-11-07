# Wii U Port — Session Prompt

> **Role of this file:** a copy-paste prompt to start a new session on this port.
> It is a *pointer*, not a copy of the state — the state lives in
> [`wiiu-handoff.md`](wiiu-handoff.md) (where to resume) and
> [`wiiu-progress.md`](wiiu-progress.md) (how it works).

---

Copy everything below into the first message of a new session.

---

Nastavljamo rad na Wii U portu DevilutionX-a u `/Users/mario/work/wiiu/DevilutionX`
(branch `wiiu`).

**Prvo pročitaj:**
1. `docs/wiiu-handoff.md` — gdje smo, što je sljedeće, kako se builda i pokreće
2. `docs/wiiu-progress.md` — tehnički detalji, root causes, pitfalls

**Kontekst u jednoj rečenici:** Diablo Shareware radi na Cemu 2.6.0 macOS
(display, asseti, tipkovnica, text input, zvuk, gameplay, savegame) iz
self-contained WUHB-a u Aroma layoutu — asseti su unutar WUHB-a
(`/vol/content/assets`), a zapisi idu u `sd:/wiiu/apps/devilutionx/`. Port se
razvija **samo na Cemu** — pravi Wii U (Aroma) testiramo tek sad kad je artefakt
ispravno zapakiran.

**Dvije stvari koje će te ugristi ako ih zaboraviš:**
1. **Cemu `tweq` bug** — Cemu trapira `tweq` i kad uvjet nije ispunjen, a GCC to
   emitira za shared_ptr use-count guard → **svaki copy shared_ptra vješa
   guest**. Simptom: tihi hang, log stane, Cemu na ~200 % CPU. Uvijek pokreni
   `tools/wiiu/cemu-tweq-workaround.py` na `devilutionx.elf` **prije**
   `elf2rpl`, i to nakon **svakog** rebuilda. Za pravi hardver ne treba.
2. **`elf2rpl` traži nestripani ELF** — strip patch se gubi na svakom
   `cmake` re-runu.

**Build + run u jednom potezu:**
```bash
tools/wiiu/wiiu-run.sh 40              # build → patch → package → run → log
tools/wiiu/wiiu-run.sh 40 --no-build   # samo re-run
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

**Sljedeći zadatak:** validacija na pravom hardveru (Wii U + Aroma). Kopiraj
`build-wiiu-release/devilutionx.wuhb` u `sd:/wiiu/apps/devilutionx/` i nakon
boota pročitaj `sd:/wiiu/apps/devilutionx/boot-diag.txt` — tu piše je li content
mount bio dostupan i koje su putanje odabrane (OSReport na hardveru ne vidimo
bez dodatne opreme). Detalji: `docs/wiiu-handoff.md` → "Next task".

**Pravila rada:**
- Debug output (OSReport `DBG`/`SND_DBG`/`SMPL_DBG`/`SFX_DBG`) **ostaje u treeu**
  — to nam je glavni alat za debugiranje. Na Wii U-u (`init.cpp`) se zove `Diag()`
  i osim u OSReport piše i u `boot-diag.txt` na SD kartici
- OSReport printf **ne podržava** `%zu`/`%lu` — koristi `%d` s castovima
- Dio fajlova je CRLF u gitu (`.gitattributes` ima `* -text`) — ne daj editoru
  da prepiše line endings
- Cemu `log.txt` se **prepisuje** svaki run, ne append-a se
