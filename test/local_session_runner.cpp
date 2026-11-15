/**
 * @file local_session_runner.cpp
 *
 * No-UI local-session runner.
 *
 * Boots the production game session through the same entry points as the game
 * (archives, options, Lua, NetInit, StartGame()) and then drives the real logic
 * tick (game_loop) with a bounded budget instead of waiting for input. It exists
 * so split-screen work can be proven without navigating menus, and so the same
 * scenario can run on a console (Wii U), where only a log file is available.
 *
 * This is the logic-only mode from the split-screen handoff: it does not draw.
 * Use test/local_session_runner to iterate on session ownership and transport;
 * rendering is covered separately.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "diablo.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "init.hpp"
#include "items.h"
#include "levels/gendung.h"
#include "loadsave.h"
#include "lua/lua_global.hpp"
#include "menu.h"
#include "monster.h"
#include "msg.h"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "player.h"
#include "portal.h"
#include "quests.h"
#include "tables/itemdat.h"
#include "tables/misdat.h"
#include "tables/monstdat.h"
#include "tables/objdat.h"
#include "tables/playerdat.hpp"
#include "tables/spelldat.h"
#include "utils/display.h"
#include "utils/paths.h"
#include "utils/str_cat.hpp"

using namespace devilution;

namespace {

struct RunnerOptions {
	std::string dataDir;
	std::string prefDir;
	std::string resultPath;
	uint32_t hero = 0;
	int ticks = 50;
	int walkTiles = 0;
	int checkpointInterval = 0;
	bool showHelp = false;
};

FILE *ResultFile = nullptr;

void Emit(std::string_view tag, std::string_view fields = {})
{
	std::string line = StrCat("{\"tag\":\"", tag, "\"");
	if (!fields.empty()) {
		line += ',';
		line += fields;
	}
	line += "}\n";
	std::fputs(line.c_str(), stdout);
	std::fflush(stdout);
	if (ResultFile != nullptr) {
		std::fputs(line.c_str(), ResultFile);
		std::fflush(ResultFile);
	}
}

int Fail(std::string_view message)
{
	Emit("RESULT", StrCat("\"status\":\"FAIL\",\"error\":\"", message, "\""));
	if (ResultFile != nullptr) {
		std::fclose(ResultFile);
		ResultFile = nullptr;
	}
	return 1;
}

void PrintHelp()
{
	std::printf(
	    "local_session_runner — run a real game session without UI\n"
	    "\n"
	    "  --data-dir <dir>   directory that contains the MPQs (default: executable dir)\n"
	    "  --pref-dir <dir>   save/config directory (required; must contain the hero save)\n"
	    "  --result <path>    also write JSONL here (default: <pref-dir>/local_session_result.jsonl)\n"
	    "  --hero <n>         hero save number (default: 0)\n"
	    "  --ticks <n>        logic ticks to run (default: 50)\n"
	    "  --walk <n>         ask the hero to walk n tiles east before ticking (default: 0)\n"
	    "  --checkpoint <n>   emit a checkpoint every n ticks (default: ticks/4)\n"
	    "\n"
	    "The hero must already exist; this runner deliberately has no UI and no\n"
	    "hero-creation path.\n");
}

bool ParseArgs(int argc, char **argv, RunnerOptions &out)
{
	for (int i = 1; i < argc; i++) {
		const std::string_view arg = argv[i];
		auto needValue = [&](std::string_view name) -> const char * {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", std::string(name).c_str());
				return nullptr;
			}
			return argv[++i];
		};
		if (arg == "--help" || arg == "-h") {
			out.showHelp = true;
		} else if (arg == "--data-dir") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.dataDir = value;
		} else if (arg == "--pref-dir") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.prefDir = value;
		} else if (arg == "--result") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.resultPath = value;
		} else if (arg == "--hero") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.hero = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
		} else if (arg == "--ticks") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.ticks = std::atoi(value);
		} else if (arg == "--walk") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.walkTiles = std::atoi(value);
		} else if (arg == "--checkpoint") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.checkpointInterval = std::atoi(value);
		} else {
			std::fprintf(stderr, "unknown argument: %s\n", std::string(arg).c_str());
			return false;
		}
	}
	if (!out.showHelp && out.prefDir.empty()) {
		std::fprintf(stderr, "--pref-dir is required\n");
		return false;
	}
	if (out.ticks <= 0) {
		std::fprintf(stderr, "--ticks must be positive\n");
		return false;
	}
	if (out.checkpointInterval <= 0)
		out.checkpointInterval = std::max(1, out.ticks / 4);
	return true;
}

[[nodiscard]] std::string DescribePlayer()
{
	if (MyPlayer == nullptr)
		return "\"player\":null";
	return std::format(
	    "\"level\":{},\"x\":{},\"y\":{},\"hp\":{},\"mana\":{}",
	    static_cast<int>(MyPlayer->getCharacterLevel()),
	    MyPlayer->position.tile.x,
	    MyPlayer->position.tile.y,
	    MyPlayer->_pHitPoints.raw(),
	    MyPlayer->_pMana.raw());
}

[[nodiscard]] std::string DescribeWorld()
{
	return std::format(
	    "\"currlevel\":{},\"leveltype\":{},\"monsters\":{},\"items\":{}",
	    static_cast<int>(currlevel),
	    static_cast<int>(leveltype),
	    static_cast<int>(ActiveMonsterCount),
	    static_cast<int>(ActiveItemCount));
}

void EmitCheckpoint(int tick)
{
	Emit("CHECKPOINT", StrCat("\"tick\":", tick, ",", DescribePlayer(), ",", DescribeWorld()));
}

} // namespace

int main(int argc, char **argv)
{
	RunnerOptions opts;
	if (!ParseArgs(argc, argv, opts)) {
		PrintHelp();
		return 2;
	}
	if (opts.showHelp) {
		PrintHelp();
		return 0;
	}

	std::error_code ec;
	std::filesystem::create_directories(opts.prefDir, ec);
	if (ec) {
		std::fprintf(stderr, "cannot create %s: %s\n", opts.prefDir.c_str(), ec.message().c_str());
		return 2;
	}

	if (opts.resultPath.empty())
		opts.resultPath = opts.prefDir + "/local_session_result.jsonl";
	ResultFile = std::fopen(opts.resultPath.c_str(), "w");
	if (ResultFile == nullptr) {
		std::fprintf(stderr, "cannot write %s\n", opts.resultPath.c_str());
		return 2;
	}

	Emit("BOOT", StrCat("\"build\":\"", __DATE__ " " __TIME__, "\""));

	HeadlessMode = true;

	if (SDL_Init(SDL_INIT_EVENTS) != 0) {
		return Fail(StrCat("SDL_Init failed: ", SDL_GetError()));
	}

	if (!opts.dataDir.empty())
		paths::SetBasePath(opts.dataDir);
	paths::SetPrefPath(opts.prefDir);
	paths::SetConfigPath(opts.prefDir);

	LoadCoreArchives();
	LoadGameArchives();
	if (!HaveMainData()) {
		return Fail("no spawn.mpq or DIABDAT.MPQ found");
	}
	// The archives are the authority on whether this is the shareware data set;
	// options and the headless runner must not silently flip it.
	const bool archivesSpawn = gbIsSpawn;

	InitKeymapActions();
	LoadOptions();
	LuaInitialize();

	gbMusicOn = false;
	gbSoundOn = false;
	gbIsSpawn = archivesSpawn;
	gbIsHellfire = false;

	LoadSpellData();
	LoadPlayerDataFiles();
	LoadMissileData();
	LoadMonsterData();
	LoadItemData();
	LoadObjectData();

	forceResolution = Size { 640, 480 };
	AdjustToScreenGeometry(forceResolution);

	gbLoadGame = false;
	gSaveNumber = opts.hero;

	Emit("SESSION", StrCat(
	                  "\"hero\":", opts.hero,
	                  ",\"spawn\":", archivesSpawn ? "true" : "false",
	                  ",\"hellfire\":false,\"ticks\":", opts.ticks,
	                  ",\"screen\":\"640x480\""));

	if (!NetInit(/*bSinglePlayer=*/true)) {
		NetClose();
		return Fail("NetInit failed");
	}

	InitLevels();
	InitQuests();
	InitPortals();
	InitDungMsgs(*MyPlayer);
	DeltaSyncJunk();
	giNumberOfLevels = gbIsHellfire ? 25 : 17;

	GameLoopBegin(WM_DIABNEWGAME);
	EmitCheckpoint(0);

	const Point startTile = MyPlayer->position.tile;
	if (opts.walkTiles != 0) {
		const Point target { startTile.x + opts.walkTiles, startTile.y };
		MakePlrPath(*MyPlayer, target, /*endspace=*/true);
		MyPlayer->destAction = ACTION_WALK;
		Emit("ACTION", StrCat("\"walkTo\":[", target.x, ",", target.y, "]"));
	}

	for (int tick = 1; tick <= opts.ticks; tick++) {
		ProcessGameMessagePackets();
		game_loop(/*bStartup=*/false);
		if (tick % opts.checkpointInterval == 0)
			EmitCheckpoint(tick);
	}

	const bool moved = MyPlayer->position.tile != startTile;
	const std::string finalPlayer = DescribePlayer();
	GameLoopEnd();
	NetClose();

	Emit("FINAL", finalPlayer);
	if (opts.walkTiles != 0) {
		Emit("MOVE_CHECK", StrCat(
		                       "\"requested\":", opts.walkTiles,
		                       ",\"moved\":", moved ? "true" : "false"));
	}
	Emit("RESULT", StrCat(
	                  "\"status\":\"", (opts.walkTiles != 0 && !moved) ? "FAIL" : "PASS",
	                  "\",\"ticks\":", opts.ticks));

	init_cleanup();
	LuaShutdown();
	SDL_Quit();

	if (ResultFile != nullptr) {
		std::fclose(ResultFile);
		ResultFile = nullptr;
	}
	return 0;
}