# DevilutionX — Wii U Port Handoff

**Datum:** 2026-10-12  
**Commit:** `b0a3b7babbdd1c10b2f7f946395e251783ae2134`  
**Repo:** `/Users/mario/work/wiiu/DevilutionX`  

---

## 1. Promijenjeni fajlovi

### Novi fajlovi (4)

| Fajl | Linija | Svrha |
|---|---|---|
| `CMake/platforms/wiiu.cmake` | 49 | Platform overrides: NONET, NOSOUND, SOL_NO_THREAD_LOCAL, LUA_USE_C89, fno-plt |
| `Source/platform/wiiu/CMakeLists.txt` | 8 | Object library stub (ukloni kad se doda pravi platform kod) |
| `Source/platform/wiiu/stub.cpp` | 2 | Placeholder dok ne treba platform source |
| `docs/wiiu-progress.md` | 140 | **Tracking fajl** — agent čita ovo na početku sesije |

### Izmenjeni fajlovi (2)

| Fajl | Promjena |
|---|---|
| `CMake/Platforms.cmake` | Dodat `if(WIIU) include(platforms/wiiu) endif()` prije Switch bloka |
| `CMakeLists.txt` | Dodat WIIU block prije Switch blocka: ELF build only, RPX/WUHB ručno |

### Ostali fajlovi (dokumentacija)

| Fajl | Svrha |
|---|---|
| `wiiu-port-analysis.md` | Detaljna analiza porta, arhitektura, usporedbe |
| `feedback2-review.md` | **Obrisan** — bio je privremeni pregled |

---

## 2. Build Status

### 2.1 Debug build — ✅ RADI

```
cmake -S . -B build-wiiu-debug \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/WiiU.cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=OFF \
  -DNONET=ON \
  -DNOSOUND=ON \
  -DBUILD_ASSETS_MPQ=OFF \
  -DDISABLE_LTO=ON

cmake --build build-wiiu-debug -j4
```

| Artifakt | Veličina | Status |
|---|---|---|
| `devilutionx.elf` | 141 MB | ✅ Generiran |
| `devilutionx.rpx` | 3.8 MB | ✅ `elf2rpl` OK |
| `devilutionx.wuhb` | 39 MB | ✅ Sa assetima + spawn.mpq |

### 2.2 Release build — ❌ FAILA

`elf2rpl` faila sa `renameRplWrap failed`. Uzrok je `--gc-sections` (dodaje se samo za Release build preko genexa). Zaobilaznica u `wiiu.cmake` pokušava stripati flag, ali ne radi jer `add_link_options` ne ide u `CMAKE_EXE_LINKER_FLAGS`.

**Treba popraviti za Release.** Moguće rješenje: dodati `-Wl,--no-gc-sections` na kraju link linije, ili postaviti `set(DEVILUTIONX_DISABLE_STRIP ON)`.

### 2.3 Problemi riješeni tokom builda

| Problem | Rješenje |
|---|---|
| SDL2_image system config → SHARED target na static-only platformi | Build iz sourcea (`DEVILUTIONX_SYSTEM_SDL_IMAGE OFF`) |
| Lua `lua_Integer` not found na PowerPC BE | `-DLUA_USE_C89` (isti fix kao 3DS/Vita/Switch) |
| sol2 `thread_local` → TLS relokacije (R_PPC_TPREL16_HA) koje elf2rpl ne podržava | `-DSOL_NO_THREAD_LOCAL=1` |
| PLT relokacije u linkanju | `-fno-plt` na CXX i C flags |

---

## 3. Cemu Test — ❌ CRASH

WUHB se učitava, title se mounta, ali odmah crasha sa **signal 5** u `FSAGetInfoByQuery`.

```
Error: signal 5:
coreinit.IPCBufPoolFree → coreinit.FSAGetInfoByQuery → FSAGetStat → InitializeFS → StringWrapper
Cemu + PPCRecompiler_virtualHLE
```

### Uočeno

- Cemu 2.6.0, macOS x86_64 (Rosetta 2 na Apple Siliconu)
- Vulkan renderer, 1280×720 swapchain kreiran
- RPL loader: 35ms link time, 22ms HLE scan
- Dva RPL loader warninga: "Section 11 uncompresses to 32 bytes but sectionSize is 44" i slično
- RPLLoader se ne javlja za sandbox

### Mogući uzroci

1. **SDL2main vs DevilutionX main()** — SDL2main ima svoj `main()` koji zove platform init pa `SDL_main()`. DevilutionX definira `main()` direktno. Mogući konflikt na Wii U gdje SDL2main pokušava ProcUIInit/FS pristup.
2. **Cemu FS emulacija** — `FSAGetStat` je coreinit API koji Cemu implementira. Moguće da ne podržava sve formate argumenata koje DevilutionX (ili SDL2 za Wii U) šalje.
3. **RPL format** — Dva RPL loader warninga mogu ukazivati na problem u elf2rpl konverziji. Treba provjeriti da li su section size mismatchi kritični.

### Sljedeći koraci za debug

- **Probati bez SDL2main** — u `Source/CMakeLists.txt` maknuti `target_link_libraries(${BIN_TARGET} PUBLIC ${SDL2_MAIN})` za Wii U (ili provjeriti da li je `SDL2_MAIN` prazan na Wii U)
- **Probati sa starijom Cemu verzijom** ili drugom platformom (Intel macOS umjesto Rossete)
- **Provjeriti RPL loader warning** — možda treba drugačiji elf2rprl flags ili prepatch ELF

---

## 4. Okruženje

### 4.1 Instalirani paketi

| Paket | Verzija |
|---|---|
| `devkitPPC` | r51-1 |
| `wut` | 1.9.1-1 |
| `wut-tools` | 1.3.5-1 |
| `wiiu-cmake` | 1.3.2-1 |
| `wiiu-sdl2` | 2.32.10-2 |
| `wiiu-sdl2_image` | 2.6.3-1 |
| `wiiu-sdl2_ttf` | 2.20.2-1 |
| `wiiu-sdl2_mixer` | 2.6.3-3 |
| `wiiu-sdl2_net` | 2.2.0-1 |
| `wiiu-sdl2_gfx` | 1.0.4-1 |
| `ppc-zlib` | 1.3.1-1 |
| `ppc-bzip2` | 1.0.8-2 |
| `ppc-libpng` | 1.6.55-1 |
| `ppc-libjpeg-turbo` | 3.1.4.1-1 |
| `ppc-libwebp` | 1.6.0-1 |
| `ppc-freetype` | 2.14.1-1 |
| `ppc-harfbuzz` | 12.3.2-1 |
| `ppc-brotli` | 1.2.0-1 |

### 4.2 Toolchain

- **Compiler:** `powerpc-eabi-g++` (GCC 16.2.0)
- **Target:** `-mcpu=750 -meabi -mhard-float`, `__WIIU__`, `__WUT__`, `ESPRESSO`
- **Linker:** wut.specs (`--emit-relocs`, `--gc-sections`, `-z nocopyreloc`)
- **CMake toolchain:** `/opt/devkitpro/cmake/WiiU.cmake`
- **Cemu:** `/Applications/Cemu.app/Contents/MacOS/Cemu` (x86_64, v2.6.0)

### 4.3 Game data

- `spawn.mpq` (24 MB, Diablo shareware) kopiran u `build-wiiu-debug/assets/`
- Očekivana lokacija na Wii U: `/vol/content/spawn.mpq` (kroz WUHB content)

---

## 5. Build Komande (brzi referenc)

### Debug build + WUHB

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC="$DEVKITPRO/devkitPPC"
export PATH="$DEVKITPRO/tools/bin:$PATH"

cmake -S . -B build-wiiu-debug \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/WiiU.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=OFF \
  -DNONET=ON \
  -DNOSOUND=ON \
  -DBUILD_ASSETS_MPQ=OFF \
  -DDISABLE_LTO=ON

cmake --build build-wiiu-debug -j4

# Ručna RPX/WUHB konverzija (ELF build only je automatski)
/opt/devkitpro/tools/bin/elf2rpl build-wiiu-debug/devilutionx.elf build-wiiu-debug/devilutionx.rpx

cp /putanja/do/spawn.mpq build-wiiu-debug/assets/

/opt/devkitpro/tools/bin/wuhbtool build-wiiu-debug/devilutionx.rpx build-wiiu-debug/devilutionx.wuhb \
  --name "DevilutionX" --author "Devilution Team" \
  --content build-wiiu-debug/assets

# Test na Cemu
/Applications/Cemu.app/Contents/MacOS/Cemu -g build-wiiu-debug/devilutionx.wuhb
```

### Clean

```bash
rm -rf build-wiiu-debug build-wiiu-release
```

---

## 6. Izmjenjeni fajlovi (puni diffovi)

### `CMake/Platforms.cmake`
Dodato `if(WIIU) include(platforms/wiiu) endif()` prije Switch bloka.

### `CMakeLists.txt`
Dodat WIIU block prije Switch bloka:
```cmake
if(WIIU)
  add_custom_command(TARGET ${BIN_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "WIIU: elf built. Run conversion manually."
  )
endif()
```

### `CMake/platforms/wiiu.cmake`
Cijeli fajl (49 linija) — vidi iznad u Sekciji 1.

### `Source/platform/wiiu/CMakeLists.txt`
```cmake
include(functions/devilutionx_library)
add_devilutionx_object_library(libdevilutionx_wiiu stub.cpp)
target_link_libraries(libdevilutionx_wiiu PUBLIC DevilutionX::SDL)
```

### `Source/platform/wiiu/stub.cpp`
```c
// Wii U platform stub — remove once actual platform source files are added.
```

---

## 7. Faze implementacije (iz docs/wiiu-progress.md)

- **Faza 0** ✅ — Okruženje spremno, sandbox buildan i testiran (crveni ekran na Cemu)
- **Faza 1** ✅ — CMake integracija, ELF/RPX/WUHB generirani. **Cemu crash** (signal 5 u FSAGetInfoByQuery) — vidi Sekciju 3
- **Faze 2-5** ❌ — Nisu pokretane

---

## 8. Što je sve provjereno i potvrđeno (source-reviewed)

Na osnovu ranijih korekcija (feedback2.md i handoff dokumenta):

| Tvrdnja | Status |
|---|---|
| `Source/CMakeLists.txt` ima 1,201 liniju, ne 30,000 | ✅ |
| `wiiu-sdl2_image` postoji i instaliran je | ✅ |
| `ppc-zlib`, `ppc-bzip2`, `ppc-libpng` su instalirani | ✅ |
| `UNPACKED_MPQS` ne uklanja SDL2_image dependency | ✅ |
| SDL_audiolib treba samo `dr_wav` + `dr_mp3`, nema external codec deps | ✅ |
| Big Endian već podržan (Amiga, endian_swap.hpp, BE grane u kodu) | ✅ |
| WUT socket API = `socket_lib_init()` / `socket_lib_finish()` (`<nsysnet/_socket.h>`) | ✅ |
| SDL2 Wii U driver sam upravlja ProcUI lifecycle (ne zvati ručno) | ✅ |
| `SDL2_image` config ima SHARED IMPORTED target mismatch | ✅ |
| Host `cmake` je dostupan | ✅ |
| Cemu je x86_64 binary (radi kroz Rosettu 2) | ✅ |
| `devkitPPC` je r51-1 (ne r50-1) | ✅ |