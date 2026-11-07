/**
 * @file init.cpp
 * @brief Wii U platform init: resolves the game's data/config/save paths.
 *
 * IMPORTANT: SDL2 for Wii U is built with the *dummy* filesystem backend
 * (see SDL_sysfilesystem.o in libSDL2.a), so SDL_GetBasePath() and
 * SDL_GetPrefPath() ALWAYS return NULL and set SDL_UNSUPPORTED. Every path
 * has to be resolved by hand here.
 *
 * Two layouts are supported, and the one to use is detected at runtime:
 *
 * 1. WUHB / Aroma ("self-contained"):
 *      /vol/content/assets/...                   read-only, from the WUHB
 *      /vol/external01/wiiu/apps/devilutionx/    writable: config, saves, mods
 *
 *    `spawn.mpq` lives inside `/vol/content/assets/`, which is why BasePath
 *    and AssetsPath both point there (the MPQ lookup only searches
 *    BasePath/PrefPath/ConfigPath/CWD, and duplicating a 24 MB MPQ just to
 *    satisfy that would be silly).
 *
 * 2. Plain SD card / dev builds (no content partition, e.g. elf2rpl + SD):
 *      <sd>/assets/...        found through CWD-relative paths
 *      <sd>/diablo.ini, <sd>/save_*.sv
 *
 *    This is the historical behaviour and stays the fallback.
 */
#include "platform/wiiu/init.hpp"

#include <cstdio>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>

#ifdef __WIIU__
#include <coreinit/debug.h>
#define DBG(fmt, ...) OSReport("WIIUINIT: " fmt "\n", ##__VA_ARGS__)
#else
#define DBG(fmt, ...) ((void)0)
#endif

#include "utils/paths.h"

namespace {

/// Read-only game data bundled into the WUHB (`wuhbtool --content`).
constexpr const char *ContentAssetsPath = "/vol/content/assets";
/// Writable directory on the SD card (created on first run).
constexpr const char *SdDataPath = "/vol/external01/wiiu/apps/devilutionx";

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

bool DirExists(const std::string &path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}

bool FileExists(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

/// Creates `path` (and its parents) if needed. Returns true if it exists afterwards.
bool EnsureDir(const std::string &path)
{
	std::string accumulated;
	for (std::size_t i = 0; i < path.size(); ++i) {
		accumulated += path[i];
		const bool isLast = i + 1 == path.size();
		if (path[i] != '/' && !isLast)
			continue;
		std::string dir = accumulated;
		while (!dir.empty() && dir.back() == '/')
			dir.pop_back();
		if (dir.empty())
			continue;
		// Failures are expected for volume roots such as "/vol" (EIO), so the
		// return value is deliberately ignored and the result checked below.
		(void)mkdir(dir.c_str(), 0777);
	}
	const bool ok = DirExists(path);
	if (!ok)
		DBG("could not create '%s'", path.c_str());
	return ok;
}

/// Actually writes a file, because `access()` lies on some FAT implementations.
bool IsWritableDir(const std::string &path)
{
	const std::string probe = path + "/.write_test";
	FILE *file = fopen(probe.c_str(), "wb");
	if (file == nullptr)
		return false;
	fclose(file);
	remove(probe.c_str());
	return true;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void LogExists(const char *label, const char *path)
{
	DBG("  %-28s '%s' -> %s", label, path, FileExists(path) ? "YES" : "no");
}

void ListDir(const char *path, int maxEntries = 16)
{
	DIR *dir = opendir(path);
	if (dir == nullptr) {
		DBG("  listdir '%s' -> FAILED", path);
		return;
	}
	int n = 0;
	while (struct dirent *ent = readdir(dir)) {
		if (++n > maxEntries) {
			DBG("  listdir '%s' -> ... (truncated at %d)", path, maxEntries);
			break;
		}
		DBG("    [%d] %s", static_cast<int>(ent->d_type), ent->d_name);
	}
	closedir(dir);
	if (n == 0)
		DBG("  listdir '%s' -> empty", path);
}

void LogEnvironment()
{
	DBG("=== Wii U environment ===");

	char cwd[512];
	DBG("  cwd = '%s'", getcwd(cwd, sizeof(cwd)) != nullptr ? cwd : "(FAILED)");

	char *base = SDL_GetBasePath();
	DBG("  SDL_GetBasePath = '%s'", base != nullptr ? base : "(null)");
	SDL_ClearError();
	if (base != nullptr)
		SDL_free(base);

	char *pref = SDL_GetPrefPath("diasurgical", "devilution");
	DBG("  SDL_GetPrefPath = '%s'", pref != nullptr ? pref : "(null)");
	SDL_ClearError();
	if (pref != nullptr)
		SDL_free(pref);

	DBG("  -- volumes --");
	LogExists("content mount", "/vol/content");
	LogExists("content/assets", ContentAssetsPath);
	LogExists("content/assets/spawn.mpq", "/vol/content/assets/spawn.mpq");
	LogExists("sd root", "/vol/external01");
	LogExists("sd data dir (ours)", SdDataPath);
	LogExists("cwd-relative assets", "assets");
	LogExists("cwd-relative spawn.mpq", "spawn.mpq");

	DBG("  -- listing /vol/content --");
	ListDir("/vol/content");
	DBG("  -- listing /vol/external01 --");
	ListDir("/vol/external01");
}

// ---------------------------------------------------------------------------
// Path setup
// ---------------------------------------------------------------------------

void SetupPaths()
{
	// --- read-only game data -------------------------------------------------
	if (DirExists(ContentAssetsPath)) {
		devilution::paths::SetAssetsPath(ContentAssetsPath);
		devilution::paths::SetBasePath(ContentAssetsPath);
		DBG("assets: using WUHB content mount '%s'", ContentAssetsPath);
	} else {
		// `paths::AssetsPath()` defaults to "assets/" and `paths::BasePath()` to "",
		// i.e. both are relative to the CWD, which is the SD card root for
		// plain SD-card builds. Nothing to do.
		DBG("assets: no content mount, using CWD-relative paths");
	}

	// --- writable data (config, saves, mods, screenshots) --------------------
	const std::string dataDir = SdDataPath;
	if (EnsureDir(dataDir) && IsWritableDir(dataDir)) {
		devilution::paths::SetPrefPath(dataDir);
		devilution::paths::SetConfigPath(dataDir);
		DBG("pref/config: using '%s'", dataDir.c_str());
	} else {
		DBG("pref/config: SD data dir unusable, using CWD-relative paths");
	}
}

} // namespace

void wiiu_init()
{
	LogEnvironment();
	SetupPaths();
	wiiu_log_paths();
}

void wiiu_log_paths()
{
	DBG("=== resolved devilution paths ===");
	DBG("  BasePath   = '%s'", devilution::paths::BasePath().c_str());
	DBG("  AssetsPath = '%s'", devilution::paths::AssetsPath().c_str());
	DBG("  PrefPath   = '%s'", devilution::paths::PrefPath().c_str());
	DBG("  ConfigPath = '%s'", devilution::paths::ConfigPath().c_str());

	// Late probe: on real hardware `/vol/save` is the per-title save volume. It is
	// not available on Cemu, but if it shows up here it is a better place for saves
	// than the SD card. Not used yet — this only records whether it exists.
	DBG("  /vol/save exists=%s writable=%s", FileExists("/vol/save") ? "yes" : "no",
	    DirExists("/vol/save") && IsWritableDir("/vol/save") ? "yes" : "no");
}
