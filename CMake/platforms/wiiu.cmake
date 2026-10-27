# Wii U platform overrides for DevilutionX
set(ASAN OFF)
set(UBSAN OFF)
set(BUILD_TESTING OFF)
set(NONET ON)
set(NOSOUND ON)
set(BUILD_ASSETS_MPQ OFF)

set(DEVILUTIONX_SYSTEM_SDL2 ON)
set(DEVILUTIONX_SYSTEM_SDL_IMAGE OFF)
set(DEVILUTIONX_SYSTEM_SDL_AUDIOLIB OFF)
set(DEVILUTIONX_SYSTEM_LIBSODIUM OFF)
set(DISABLE_ZERO_TIER ON)

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

list(APPEND DEVILUTIONX_PLATFORM_SUBDIRECTORIES platform/wiiu)
list(APPEND DEVILUTIONX_PLATFORM_LINK_LIBRARIES libdevilutionx_wiiu)