/**
 * @file multi.h
 *
 * Interface of functions for keeping multiplayer games in sync.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dvlnet/leaveinfo.hpp"
#include "game_mode.hpp"
#include "msg.h"
namespace devilution {

using net::leaveinfo_t;

// Defined in player.h, forward declared here to allow for functions which operate in the context of a player.
struct Player;

// must be unsigned to generate unsigned comparisons with pnum
#define MAX_PLRS 4

/* @brief Contains info of running public game (for game list browsing) */
struct GameInfo {
	std::string name;
	GameData gameData;
	std::vector<std::string> players;
	std::optional<int> latency;
	std::optional<bool> peerIsRelayed;
};

extern bool gbSomebodyWonGameKludge;
extern uint16_t sgwPackPlrOffsetTbl[MAX_PLRS];
extern uint8_t gbActivePlayers;
extern bool gbGameDestroyed;
extern bool gbSelectProvider;
extern std::string GameName;
extern std::string GamePassword;
extern bool PublicGame;
extern uint8_t gbDeltaSender;
extern uint32_t player_state[MAX_PLRS];
extern bool IsLoopback;

// Local co-op: number of local seats in this session (0 = off) and the seat that
// currently owns the command being built (used to tag the message sender).
extern uint8_t gbLocalCoopSeats;
extern uint8_t gbLocalCoopSender;
// Dev switch for automated console checks: skip the menu and start seat 0.
extern bool gbLocalCoopAutoStart;
// Split each local seat into its own viewport (shared panel) instead of one camera.
extern bool gbLocalCoopSplitScreen;
extern bool gbLocalCoopResume;

// Per-seat hero save slot numbers (set by the lobby, used by InitLocalCoop).
extern uint32_t gbLocalCoopSaveSlot[4];

// Per-seat UI flags for local co-op.
// Each seat has its own automap, spell-select, inventory, etc. state
// so one player's UI never affects another's.
extern bool gbLocalCoopAutomapActive[4];
extern bool gbLocalCoopSpellSelect[4];
extern bool gbLocalCoopInvFlag[4];
extern bool gbLocalCoopCharFlag[4];
extern bool gbLocalCoopSpellbookFlag[4];
extern bool gbLocalCoopQuestLogOpen[4];

void SwapGameDataLE(GameData &gameData);
std::string DescribeLeaveReason(leaveinfo_t leaveReason);
std::string FormatGameSeed(const uint32_t gameSeed[4]);

void InitGameInfo();
void NetSendLoPri(uint8_t playerId, const std::byte *data, size_t size);
void NetSendHiPri(uint8_t playerId, const std::byte *data, size_t size);
void multi_send_msg_packet(uint32_t pmask, const std::byte *data, size_t size);
void multi_msg_countdown();
void multi_player_left(uint8_t pnum, leaveinfo_t reason);
void multi_net_ping();

/**
 * @return Always true for singleplayer
 */
bool multi_handle_delta();
void ProcessGameMessagePackets();
void multi_send_zero_packet(uint8_t pnum, _cmd_id bCmd, const std::byte *data, size_t size);
void NetClose();
bool NetInit(bool bSinglePlayer);
bool InitLocalCoop(GameData *gameData, uint8_t seats);

// Reads the local co-op seat count from <pref>/local_coop.ini (0 = off).
void LoadLocalCoopSettings();
void recv_plrinfo(Player &player, const TCmdPlrInfoHdr &header, bool recv);

} // namespace devilution
