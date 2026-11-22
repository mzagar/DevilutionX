# Convenience entry points for the Wii U port. Everything else is CMake:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake
#   cmake --build build
#
# `make dist` builds a distributable shareware WUHB and packages it as a
# ready-to-extract SD-card ZIP in ./dist (no Cemu needed).

.PHONY: dist wiiu-help

dist:
	@tools/wiiu/build-release.sh

wiiu-help:
	@echo "make dist   build dist/devilutionx-wiiu-<branch>-<sha>.zip"
	@echo "            (WUHB with shareware data, ready for an Aroma SD card)"
