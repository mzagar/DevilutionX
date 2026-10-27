# Wii U Port — Handoff to new session

## ⚡ Quick start (build → test)

```bash
cd /Users/mario/work/wiiu/DevilutionX/build-wiiu-release

# Disable strip (once per `cmake ..`)
sed -i '' 's|/opt/devkitpro/devkitPPC/bin/powerpc-eabi-strip|echo "SKIP_STRIP" #|' \
  CMakeFiles/devilutionx.dir/build.make

# Build + convert + package
make -j4 && elf2rpl devilutionx.elf devilutionx.rpx && \
wuhbtool $(pwd)/devilutionx.rpx $(pwd)/devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# Run on Cemu
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g /Users/mario/work/wiiu/DevilutionX/build-wiiu-release/devilutionx_sd.wuhb \
  -m /Users/mario/work/wiiu/cemu-mlc

# View debug log
grep "\[OSConsole\]" ~/Library/Application\ Support/Cemu/log.txt
```

## 🔧 Adding OSReport debug logging

The output appears in Cemu's log.txt as `[OSConsole]`.

```cpp
// Add after #includes in any .cpp:
#ifdef __WIIU__
#include <coreinit/debug.h>
#define DBG(fmt, ...) OSReport("TAG: " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

// Use anywhere:
DBG("before InitSomething");
InitSomething();
DBG("after InitSomething");
```

To remove: delete the `#ifdef __WIIU__` block and all `DBG(...)` calls.

## ✅ Current state (13 Oct 2026)

**Diablo Shareware main menu is visible on Cemu 2.6.0 macOS.**

The game loads, initialises, finds all assets from the emulated SD card,
loads Lua scripts, and enters the main loop. The Diablo title screen with
"Single Player", "Multi Player", "Replay Intro", "Quit" buttons is displayed.

## ❌ Problem: keyboard and mouse input don't work

The user cannot navigate the menu with:
- Arrow keys (up/down/enter)
- Mouse clicks
- Mouse movement

The window displays the Diablo main menu but is completely unresponsive to
keyboard and mouse input. No cursor is visible, no button highlights change,
no menu selection moves.

## Suspected root cause

The Wii U SDL port requires platform-specific initialisation for input devices:

1. **VPADInit / KPADInit** — Wii U GamePad/Pro Controller API. These are
   normally called during `SpawnWindow()` → `SDL_Init()`, but on Cemu
   the VPAD/KPAD emulation might not be triggered correctly.

2. **SDL event loop on Wii U** — SDL on Wii U uses GX2/VPAD/KPAD for
   input and display. The input path from Cemu's keyboard/mouse host input
   through the emulated Wii U to SDL might be broken or not initialised.

3. **Missing ProcUI events** — Without `WHBProcInit()` (which we call in
   our init), the HOME button overlay doesn't work, but keyboard/mouse
   should still function. We ARE calling `WHBProcInit()`, so this isn't
   the issue.

4. **GX2 vs OSScreen conflict** — The game uses GX2 for rendering. If
   GX2 is not fully initialised for input, keyboard/mouse might not
   propagate through the SDL event system.

## Tried but not the cause

- `WHBProcInit()` / `WHBMountSdCard()` — called and working (confirmed
  via OSReport logging)

- `SDL_Init(SDL_INIT_VIDEO)` — called inside `SpawnWindow()`. Additional
  flags like `SDL_INIT_JOYSTICK` and `SDL_INIT_GAMECONTROLLER` are also
  passed.

- No SDL test render runs before DiabloMain in the current version (the
  test render was removed to avoid GX2 state corruption).

## Next steps to debug

1. **Add OSReport logging for SDL input initialisation** — trace through
   `SpawnWindow()` and `SDL_Init()` to see which subsystems are enabled.

2. **Check if `SDL_PollEvent` returns events** — inside the game loop,
   log when events (SDL_KEYDOWN, SDL_MOUSEBUTTONDOWN) are received.

3. **Add `WHBLogCafeInit` back** — not critical for input but helps
   with general debugging.

4. **Try `VPADInit()` / `KPADInit()` directly** — if SDL doesn't
   initialise VPAD/KPAD on Cemu, calling them manually before
   `DiabloMain()` might enable input.

5. **Check Cemu's keyboard mapping** — In Cemu's input settings,
   ensure keyboard is mapped to Wii U GamePad buttons. By default,
   Cemu maps keyboard to Wii U Pro Controller or GamePad.

## Build reference

```bash
cd build-wiiu-release
# Disable strip (required for elf2rpl):
sed -i '' 's|powerpc-eabi-strip|echo "SKIP_STRIP" #|' \
  CMakeFiles/devilutionx.dir/build.make

make -j4
elf2rpl devilutionx.elf devilutionx.rpx
wuhbtool $(pwd)/devilutionx.rpx $(pwd)/devilutionx_sd.wuhb \
  --name "DevilutionX" --author "Devilution Team"

# SD card path (assets must be here):
SD=~/Library/Application\ Support/Cemu/sdcard
# Copy assets from clean build:
cp -r build-wiiu-clean/assets/* "$SD"/assets/
cp build-wiiu-clean/assets/spawn.mpq "$SD"/

# Run on Cemu:
"/Applications/Cemu.app/Contents/MacOS/Cemu" \
  -g devilutionx_sd.wuhb -m /Users/mario/work/wiiu/cemu-mlc

# Log:
cat ~/Library/Application\ Support/Cemu/log.txt | grep "\[OSConsole\]"
```

## Key files

| File | Role |
|------|------|
| `Source/platform/wiiu/init.cpp` | Platform init: WHBProcInit, WHBMountSdCard, SetBasePath |
| `Source/platform/wiiu/init.hpp` | Header |
| `Source/main.cpp` | `#ifdef __WIIU__` → `wiiu_init()` |
| `3rdParty/Lua/CMakeLists.txt` | `LUA_USE_C89` for Wii U (fixes Lua panic) |
| `docs/wiiu-progress.md` | Full progress log |