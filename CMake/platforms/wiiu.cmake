# Wii U platform overrides for DevilutionX
# Makes CMake/platforms/wiiu/asio_defs.cmake findable (mirrors switch.cmake).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/wiiu")

set(ASAN OFF)
set(UBSAN OFF)
set(BUILD_TESTING OFF)
# Networking is enabled ("NONET" left off) for feature parity with the other
# platforms: wut provides nsysnet (socket_lib_init + standard socket headers)
# and the console ports already solved the asio/sodium side.
# ZeroTier stays disabled -- avoiding port forwarding is what a VPN is for, and
# the lwip integration is a much bigger job than the TCP provider.
set(DISABLE_ZERO_TIER ON)
# Sound is enabled: the devkitPro SDL2 Wii U port provides an AX audio driver
# (SDL_wiiuaudio.c) and DevilutionX uses the bundled static SDL_audiolib.
set(BUILD_ASSETS_MPQ OFF)

set(DEVILUTIONX_SYSTEM_SDL2 ON)
set(DEVILUTIONX_SYSTEM_SDL_IMAGE OFF)
set(DEVILUTIONX_SYSTEM_SDL_AUDIOLIB OFF)
set(DEVILUTIONX_SYSTEM_LIBSODIUM OFF)

# Required for compilation on Wii U toolchain
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DLUA_USE_C89")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSOL_NO_THREAD_LOCAL=1")

# NOTE 2026-10-13: Debug sections are NOT the cause of the Cemu crash.
# The crash (signal 10 in coreinit::__OSFiberThreadEntry) happens identically
# with and without debug sections. See docs/wiiu-handoff.md for details.
#
# The crash is likely a Cemu 2.6.0 macOS bug triggered by binaries with
# .text section > ~1MB. Test binaries (0.73MB) work fine.
#
# This flag can still be used to reduce binary size but won't fix the crash:
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g0")

# Disable RTTI and exceptions flags that may cause issues
set(DEVILUTIONX_DISABLE_RTTI OFF)
set(DEVILUTIONX_DISABLE_EXCEPTIONS OFF)

set(PIE OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# SD-card directory this build owns (sd:/wiiu/apps/<name>/). Set it to ship
# two builds side by side on one SD card, e.g.
#   -DWIIU_APP_DIR_NAME=devilutionx-splitscreen
set(WIIU_APP_DIR_NAME "devilutionx" CACHE STRING "SD data directory name under sd:/wiiu/apps/")

list(APPEND DEVILUTIONX_PLATFORM_SUBDIRECTORIES platform/wiiu)
list(APPEND DEVILUTIONX_PLATFORM_LINK_LIBRARIES libdevilutionx_wiiu)