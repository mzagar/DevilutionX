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
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "diablo.h"
#include "engine/assets.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "controls/seat_input.h"
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

#ifdef __WIIU__
#include <sys/stat.h>

#include "platform/wiiu/init.hpp"
#endif

using namespace devilution;

namespace {

struct RunnerOptions {
	std::string dataDir;
	std::string prefDir;
	std::string resultPath;
	std::string createHero;
	uint32_t hero = 0;
	int ticks = 50;
	int seats = 1;
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
	    "  --create-hero <c>  create a disposable hero first (warrior|rogue|sorcerer|monk)\n"
	    "  --ticks <n>        logic ticks to run (default: 50)\n"
	    "  --seats <n>        local co-op seats (1-4, default: 1)\n"
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
		} else if (arg == "--create-hero") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.createHero = value;
		} else if (arg == "--hero") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.hero = static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
		} else if (arg == "--ticks") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.ticks = std::atoi(value);
		} else if (arg == "--seats") {
			const char *value = needValue(arg);
			if (value == nullptr) return false;
			out.seats = std::atoi(value);
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
#ifndef __WIIU__
	if (!out.showHelp && out.prefDir.empty()) {
		std::fprintf(stderr, "--pref-dir is required\n");
		return false;
	}
#endif
	if (out.seats < 1 || out.seats > MAX_PLRS) {
		std::fprintf(stderr, "--seats must be between 1 and %d\n", MAX_PLRS);
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

[[nodiscard]] std::string DescribePlayer(uint8_t seat)
{
	if (seat >= Players.size())
		return "\"player\":null";
	const Player &player = Players[seat];
	return std::format(
	    "\"seat\":{},\"level\":{},\"x\":{},\"y\":{},\"hp\":{},\"mana\":{}",
	    seat,
	    static_cast<int>(player.getCharacterLevel()),
	    player.position.tile.x,
	    player.position.tile.y,
	    player._pHitPoints.raw(),
	    player._pMana.raw());
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
	for (uint8_t seat = 0; seat < gbLocalCoopSeats && seat < Players.size(); ++seat)
		Emit("CHECKPOINT", StrCat("\"tick\":", tick, ",", DescribePlayer(seat), ",", DescribeWorld()));
}

std::optional<HeroClass> ParseHeroClass(std::string_view name)
{
	if (name == "warrior") return HeroClass::Warrior;
	if (name == "rogue") return HeroClass::Rogue;
	if (name == "sorcerer") return HeroClass::Sorcerer;
	if (name == "monk") return HeroClass::Monk;
	return std::nullopt;
}

/// Creates a disposable hero through the production save writer, so the runner
/// never depends on saves produced by the UI (or on the user's own saves).
bool CreateDisposableHero(HeroClass heroClass, uint32_t saveNumber)
{
	Players.resize(1);
	_uiheroinfo heroInfo {};
	heroInfo.saveNumber = saveNumber;
	CopyUtf8(heroInfo.name, "Runner", sizeof(heroInfo.name));
	heroInfo.heroclass = heroClass;
	heroInfo.level = 1;
	heroInfo.hassaved = false;
	heroInfo.spawned = gbIsSpawn;
	giNumberOfLevels = gbIsHellfire ? 25 : 17;
	return pfile_ui_save_create(&heroInfo);
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

#ifdef __WIIU__
	wiiu_init();
	if (opts.prefDir.empty()) {
		// Keep runner saves away from the player's own characters.
		opts.prefDir = StrCat(paths::PrefPath(), "runner");
	}
	if (opts.createHero.empty())
		opts.createHero = "warrior";
	::mkdir(opts.prefDir.c_str(), 0777);
#else
	std::error_code ec;
	std::filesystem::create_directories(opts.prefDir, ec);
	if (ec) {
		std::fprintf(stderr, "cannot create %s: %s\n", opts.prefDir.c_str(), ec.message().c_str());
		return 2;
	}
#endif

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

	bool heroCreated = false;
	const std::optional<HeroClass> heroClass = ParseHeroClass(opts.createHero);
	if (!opts.createHero.empty() && !heroClass.has_value())
		return Fail(StrCat("unknown hero class: ", opts.createHero));
	for (int seat = 0; seat < opts.seats; ++seat) {
		if (!opts.createHero.empty()) {
			if (!CreateDisposableHero(*heroClass, static_cast<uint32_t>(seat)))
				return Fail(StrCat("could not create the disposable hero for seat ", seat));
			heroCreated = true;
		}
	}

	gbLoadGame = false;
	gSaveNumber = opts.hero;
	gbLocalCoopSeats = static_cast<uint8_t>(opts.seats);

	Emit("SESSION", StrCat(
	                  "\"hero\":", opts.hero,
	                  ",\"seats\":", opts.seats,
	                  ",\"spawn\":", archivesSpawn ? "true" : "false",
	                  ",\"createdHero\":", heroCreated ? "true" : "false",
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

	std::array<Point, MAX_PLRS> startTiles {};
	for (uint8_t seat = 0; seat < gbLocalCoopSeats; ++seat)
		startTiles[seat] = Players[seat].position.tile;

	if (opts.walkTiles != 0 && opts.seats == 1) {
		const Point target { startTiles[0].x + opts.walkTiles, startTiles[0].y };
		MakePlrPath(*MyPlayer, target, /*endspace=*/true);
		MyPlayer->destAction = ACTION_WALK;
		Emit("ACTION", StrCat("\"walkTo\":[", target.x, ",", target.y, "]"));
	}
	if (opts.walkTiles != 0 && opts.seats > 1) {
		// Drive each local seat through the production per-seat input path.
		for (uint8_t seat = 0; seat < gbLocalCoopSeats; ++seat)
			SeatInput::Instance().OnAxis(seat, SeatInput::SeatAxisLeftX, opts.walkTiles > 0 ? 32767.0F : -32767.0F);
		Emit("ACTION", StrCat("\"seatAxisX\":", opts.walkTiles > 0 ? 32767 : -32767));
	}

	for (int tick = 1; tick <= opts.ticks; tick++) {
		ProcessGameMessagePackets();
		game_loop(/*bStartup=*/false);
		if (tick % opts.checkpointInterval == 0)
			EmitCheckpoint(tick);
	}

	bool allMoved = true;
	for (uint8_t seat = 0; seat < gbLocalCoopSeats && seat < Players.size(); ++seat) {
		const bool moved = Players[seat].position.tile != startTiles[seat];
		if (!moved)
			allMoved = false;
		Emit("FINAL", DescribePlayer(seat));
		if (opts.walkTiles != 0)
			Emit("MOVE_CHECK", StrCat("\"seat\":", seat, ",\"moved\":", moved ? "true" : "false"));
	}
	GameLoopEnd();
	NetClose();

	Emit("RESULT", StrCat(
	                  "\"status\":\"", (opts.walkTiles != 0 && !allMoved) ? "FAIL" : "PASS",
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