# Wii U Port — Session Prompt

Copy and paste this file as the first message in a new session to continue the Wii U port work.

---

## Current state: Diablo (Shareware) runs on Cemu 2.6.0 macOS

- ✅ **Display** — SDL/GX2 renderer works, Diablo main menu visible
- ✅ **Assets** — loaded from emulated SD card (`/vol/external01`, mapped to `~/Library/Application Support/Cemu/sdcard`)
- ✅ **Lua** — fixed with `LUA_USE_C89` for PowerPC (was: `attempt to index a nil value`)
- ✅ **Keyboard/mouse** — works via Cemu Input Settings (map keyboard as Controller 1 → GamePad)
- ✅ **Text input** — custom `keyboard.cpp` bypasses broken system swkbd, uses SDL_TEXTINPUT events directly
- ✅ **OSReport debug logging** — appears in Cemu log.txt as `[OSConsole]`
- ✅ **Build workflow** — documented (strip fix before elf2rpl)
- ❌ **Sound** — NOSOUND=ON (disabled)
- ❌ **Network** — NONET=ON (disabled, no multiplayer)

## ⚡ Quick build & test

```bash
cd /Users/mario/work/wiiu/DevilutionX/build-wiiu-release

# Disable strip (once per cmake):
sed -i '' 's|/opt/devkitpro/devkitPPC/bin/powerpc-eabi-strip|echo "SKIP_STRIP" #|' \
  CMakeFiles/devilutionx.dir/build.make

# Build + convert + package:
make -j4 && elf2rpl devilutionx.elf devilutionx.rpx && \
wuhbtool $(pwd)/devilutionx.rpx $(pwd)/devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# Run on Cemu:
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g /Users/mario/work/wiiu/DevilutionX/build-wiiu-release/devilutionx_sd.wuhb \
  -m /Users/mario/work/wiiu/cemu-mlc

# Check log:
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

## 🗺️ Project structure (Wii U specific)

```
Source/
  main.cpp                          — entry point, calls wiiu_init()
  diablo.cpp                        — DiabloMain (has OSReport debug logs)
  lua/lua_global.cpp                — LuaInitialize (has OSReport debug logs)
  DiabloUI/diabloui.cpp             — UI loop, keyboard dispatch
  DiabloUI/text_input.cpp           — text input event handler
  platform/wiiu/
    init.cpp                        — WHBProcInit + WHBMountSdCard + SetBasePath
    init.hpp                        — header
    keyboard.cpp                    — text input (bypasses system swkbd)
    keyboard.hpp                    — header
    stub.cpp                        — empty placeholder
    CMakeLists.txt                  — platform lib build

CMake/platforms/wiiu.cmake          — Wii U platform config
3rdParty/Lua/CMakeLists.txt         — has WIIU in LUA_USE_C89 list (important!)
docs/wiiu-progress.md               — full progress log
docs/wiiu-handoff.md                — handoff context
```

## 🔧 Adding OSReport debug logging

```cpp
// In any .cpp, after #includes:
#ifdef __WIIU__
#include <coreinit/debug.h>
#define DBG(fmt, ...) OSReport("Tag: " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

// Usage:
DBG("step 1: value=%d", x);
```

Output appears in `log.txt` as `[OSConsole]`.

## 🎯 Prioritized next steps

### 1. Test on real Wii U (Aroma)
1. Copy `build-wiiu-release/devilutionx_sd.wuhb` to `SD:/wiiu/apps/devilutionx/`
2. Copy assets + spawn.mpq to same folder (or SD card root)
3. Launch via Aroma Homebrew Launcher
4. Check if OSReport output works via UDP or serial

### 2. Assets from WUHB content (not SD card)
Current issue: assets must be on SD card. CWD is `/code/` not `/content/`.
Fix options:
- Use `FSBindMount` to bind `/content/` to an accessible path
- Or put assets inside the WUHB root at `/content/` AND set BasePath to `/content/`
- Or discover the correct CWD and use `SetBasePath`

### 3. Enable sound
In `CMake/platforms/wiiu.cmake`:
```
set(NOSOUND ON)  →  set(NOSOUND OFF)
```
Will need SDL_mixer or similar for audio output.

### 4. Enable network
In `CMake/platforms/wiiu.cmake`:
```
set(NONET ON)  →  set(NONET OFF)
```
Will need `nsysnet` socket init (like Switch's `switch_enable_network()`).

### 5. Remove OSReport debug logs
- `Source/diablo.cpp` — delete `#ifdef __WIIU__` block and `DBG()` calls
- `Source/lua/lua_global.cpp` — same
- Or gate them behind a `#define DEBUG_WIIU` flag

### 6. Docking / TV-DRC handling
Switch has `docking.cpp`. Wii U needs similar for TV/DRC mode changes.

## ⚠️ Common pitfalls

| Issue | Fix |
|-------|-----|
| `elf2rpl` → `renameRplWrap failed` | Strip must be **disabled** before conversion |
| Lua panic `attempt to index a nil value` | Need `LUA_USE_C89` for PowerPC |
| Text input ✓ doesn't work | swkbd broken on Cemu — use keyboard.cpp (SDL events) |
| Assets not found | CWD is `/code/`, not `/content/`. Use `SetBasePath` to SD card path |
| FSShutdown → signal 10 crash | Don't call `FSShutdown` on Cemu. Use WHB functions instead |
| Black screen with no errors | Version check failed (no ASSETS_VERSION) or assets in wrong location |

## Cemu paths

| Host path | Emulated path |
|-----------|---------------|
| `~/Library/Application Support/Cemu/sdcard/` | `/vol/external01` (SD card) |
| `~/Library/Application Support/Cemu/sdcard/assets/` | `/vol/external01/assets/` |
| `~/Library/Application Support/Cemu/sdcard/spawn.mpq` | `/vol/external01/spawn.mpq` |
| `~/Library/Application Support/Cemu/log.txt` | Cemu log (OSConsole lines here) |
| `~/Library/Application Support/Cemu/settings.xml` | Add `<open_pad>true</open_pad>` for GamePad window |

## Cemu input setup
- Controller 1 → GamePad (or Pro Controller)
- Map keyboard: Arrow keys → D-Pad, Enter → A button, "4" → A button
- This allows navigating menus, clicking buttons, and typing text