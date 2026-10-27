# Wii U Port — Progress

## ⚡ Quick start (build → test in one go)

```bash
cd /Users/mario/work/wiiu/DevilutionX/build-wiiu-release

# 1. Disable strip (only needed once per `cmake ..`)
sed -i '' 's|/opt/devkitpro/devkitPPC/bin/powerpc-eabi-strip|echo "SKIP_STRIP" #|' \
  CMakeFiles/devilutionx.dir/build.make

# 2. Build, convert, package (all in one)
make -j4 && \
elf2rpl devilutionx.elf devilutionx.rpx && \
wuhbtool $(pwd)/devilutionx.rpx $(pwd)/devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# 3. Run on Cemu (wait 10-15s for title screen)
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g /Users/mario/work/wiiu/DevilutionX/build-wiiu-release/devilutionx_sd.wuhb \
  -m /Users/mario/work/wiiu/cemu-mlc

# 4. Check log for debug output
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

## 🔧 Adding/removing OSReport debug logging

When you need to trace where code hangs or crashes, add OSReport calls.
The output appears in Cemu's log.txt tagged with `[OSConsole]`.

### Pattern for any .cpp file:
```cpp
// At top of file, after #includes:
#ifdef __WIIU__
#include <coreinit/debug.h>
#define DBG(fmt, ...) OSReport("MyFile: " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

// Anywhere in the code:
DBG("step 1");
DBG("value = %d", someVariable);
```

### Current files with debug logging (remove when done):
- `Source/diablo.cpp` — DiabloMain step-by-step
- `Source/lua/lua_global.cpp` — LuaInitialize step-by-step

To remove: delete the `#ifdef __WIIU__` block at the top and all `DBG(...)` calls.

## Stanje: ✅ Diablo (Shareware) radi na Cemu 2.6.0 macOS!

**Diablo main menu je vidljiv i odzivan.**
- ✅ Binary se učita bez crasha
- ✅ SDL renderer radi (crveni test screen ranije potvrđen)
- ✅ Platform init (WHBProcInit, WHBMountSdCard) radi
- ✅ Assets pronađeni sa SD kartice (`/vol/external01`)
- ✅ LuaInitialize prolazi bez panica (LUA_USE_C89 fix)
- ✅ Game loop radi — vidi se Diablo Shareware glavni meni
- ❌ **Keyboard/mouse ne radi** — ne možeš navigirati po meniju

### Build

```
build-wiiu-release/devilutionx.elf    — unstripped ELF (~200MB, za konverziju)
build-wiiu-release/devilutionx.rpx    — RPX (~3.7MB)
build-wiiu-release/devilutionx.wuhb   — WUHB (~3.7MB, bez contenta)
build-wiiu-release/devilutionx_sd.wuhb — WUHB (3.7MB, asseti na SD kartici)
```

### Arhitektura rješenja

**Platform init (`Source/platform/wiiu/init.cpp`):**
1. `WHBProcInit()` — omogućava event loop (HOME button, suspend)
2. `WHBMountSdCard()` — montira SD karticu
3. `devilution::paths::SetBasePath("/vol/external01/")` — postavlja putanje na SD karticu
   - Isto za PrefPath, ConfigPath, AssetsPath
4. `OSReport()` logging se pojavljuje u Cemu → `[OSConsole]` u `log.txt`

**Assetovi na SD kartici (`~/Library/Application Support/Cemu/sdcard/`):**
```
spawn.mpq              — game data (Shareware)
assets/ASSETS_VERSION   — "1\n" (prolazi version check)
assets/fonts/           — fontovi za renderiranje
assets/ui_art/          — UI elementi (dugmad, prozori)
assets/lua/             — Lua skripte (inspect, events, modovi)
assets/data/            — data fajlovi
assets/txtdata/         — text data (TSV fajlovi)
assets/gendata/         — cutscene/game data
assets/arena/           — arena mode data
assets/plrgfx/          — player graphics
assets/lua_internal/    — internal lua scripts
assets/*.gmo            — translation files
```

**ELF → RPX konverzija:**
- `powerpc-eabi-strip` **mora biti isključen** prije `elf2rpl`
- Strip uklanja `.rela.*` sekcije koje `elf2rpl` treba
- Zakrpa: `sed -i '' 's|powerpc-eabi-strip|echo "SKIP_STRIP" #|' CMakeFiles/devilutionx.dir/build.make`

**LUA_USE_C89 fix:**
- Dodan `WIIU` u listu platformi u `3rdParty/Lua/CMakeLists.txt`
- Rješava Lua panic (`attempt to index a nil value`) na PowerPC EABI
- Problem: Lua-ov `longjmp` za error handling ne radi ispravno na PowerPC
- `LUA_USE_C89` izbjegava C99-specific features koje uzrokuju problem

### Šta je mijenjano

| File | Šta |
|------|-----|
| `Source/platform/wiiu/init.cpp` | WHBProcInit + WHBMountSdCard + SetBasePath |
| `Source/platform/wiiu/init.hpp` | Deklaracija |
| `Source/main.cpp` | `#ifdef __WIIU__` → `wiiu_init()` |
| `Source/platform/wiiu/CMakeLists.txt` | Dodan `init.cpp` |
| `CMake/platforms/wiiu.cmake` | Wii U build config (manje izmjene) |
| `3rdParty/Lua/CMakeLists.txt` | Dodan `WIIU` u `LUA_USE_C89` listu |
| `Source/utils/paths.cpp` | Nije mijenjan — koristimo `SetBasePath` API |
| `Source/diablo.cpp` | **Debug** OSReport logovi (ukloniti kasnije) |
| `Source/lua/lua_global.cpp` | **Debug** OSReport logovi (ukloniti kasnije) |

### Build & test workflow

```bash
# 1. Build ELF (bez stripanja!)
cd build-wiiu-release
sed -i '' 's|powerpc-eabi-strip|echo "SKIP_STRIP" #|' \
  CMakeFiles/devilutionx.dir/build.make
make -j4

# 2. Konverzija u RPX i WUHB
elf2rpl devilutionx.elf devilutionx.rpx
wuhbtool $(pwd)/devilutionx.rpx $(pwd)/devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# 3. Priprema SD kartice (prvi put)
# Kopiraj assetove iz clean builda:
SD=~/Library/Application\ Support/Cemu/sdcard
rm -rf "$SD"/assets "$SD"/spawn.mpq
cp -r build-wiiu-clean/assets/* "$SD"/assets/   # sve osim spawn.mpq
cp build-wiiu-clean/assets/spawn.mpq "$SD"/      # spawn.mpq u root

# 4. Pokretanje na Cemu
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g /Users/mario/work/wiiu/DevilutionX/build-wiiu-release/devilutionx_sd.wuhb \
  -m /Users/mario/work/wiiu/cemu-mlc

# 5. Logging se vidi u:
cat ~/Library/Application\ Support/Cemu/log.txt
# OSReport poruke su označene sa [OSConsole]
```

### Poznati problemi

1. **Keyboard/mouse ne radi** — SDL eventovi sa tastature/miša se ne processiraju.
   ~~Vjerovatno treba `VPADInit()` i/ili `KPADInit()` za Wii U gamepad emulaciju,
   ili SDL event loop nije povezan sa Cemu-ovim input sistemom.~~
   ✅ **RIJEŠENO** — keyboard radi kad se mapira kao Controller 1 (GamePad) u Cemu Input Settings.

2. **Text input (swkbd) ne potvrđuje unos** — Wii U sistemska swkbd ima buggy ✓
   na Cemu 2.6 macOS. Tipke rade, backspace radi, ali ✓ ne zatvara dijalog.
   ✅ **RIJEŠENO** — kreiran `keyboard.cpp` koji preskače swkbd i koristi
   direktno SDL_TEXTINPUT eventove (kao PC verzija).

2. **Assetovi moraju biti na SD kartici** — ne rade iz WUHB content direktorija
   (CWD nije `/content/`). Trebalo bi riješiti da rade i iz WUHB-a.

3. **ELF bez stripanja je ~200MB** — samo za konverziju, RPX/WUHB su ~3.7MB.
   Treba automatizirati build proces da se strip radi nakon konverzije.

4. **FSA FSShutdown crasha** — `FSShutdown()` izaziva signal 10 na Cemu 2.6 macOS.
   Zato koristimo WHB funkcije koje interne FSA handleaju bez manualnog shutdowna.

### Testing

```
Cemu:     /Applications/Cemu.app
MLC:      /Users/mario/work/wiiu/cemu-mlc
SD card:  ~/Library/Application Support/Cemu/sdcard/ → /vol/external01
Log:      ~/Library/Application Support/Cemu/log.txt (traži [OSConsole])
```