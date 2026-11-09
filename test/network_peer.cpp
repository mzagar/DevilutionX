/**
 * @file network_peer.cpp
 * @brief No-UI network peer harness: exercises the real dvlnet TCP provider.
 *
 * This is the T2/T3 test program described in the Wii U multiplayer handoff.
 * It drives the same SNet API, the same TCP provider and the same packet codec
 * as the game, but without the menus, hero dialog, archives or gameplay
 * simulation, so a console peer can run it without any UI interaction.
 *
 * One process = one peer (the SNet instance and much of the engine state are
 * global). `test/run_network_scenario.py` starts a host and N-1 clients and
 * collects the structured JSONL results.
 *
 * Configuration is read from a key=value file (see `Config`) so the same
 * binary can be launched on a console, where there is no usable command line.
 * Command-line `--key=value` arguments override the file.
 *
 * Every peer emits lines of the form
 *
 *   {"event":"HOST_READY","ts":1234,...}
 *
 * to stdout and to the result file. The last line is always
 *
 *   {"event":"RESULT","status":"PASS"|"FAIL","reason":"..."}
 *
 * Exit code: 0 on PASS, 1 on FAIL, 2 on a setup/usage error.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "config.h"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "multi.h"
#include "options.h"
#include "player.h"
#include "storm/storm_net.hpp"
#include "utils/endian_swap.hpp"

#ifdef __WIIU__
#include <coreinit/debug.h>

#include "platform/wiiu/network.hpp"
#ifdef PACKET_ENCRYPTION
#include "platform/wiiu/random.hpp"
#endif
#endif

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace devilution {
namespace {

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

std::FILE *g_resultFile = nullptr;

std::string JsonEscape(const std::string &value)
{
	std::string out;
	out.reserve(value.size() + 8);
	for (const char c : value) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
				out += buf;
			} else {
				out += c;
			}
		}
	}
	return out;
}

uint32_t NowMillis()
{
	return SDL_GetTicks();
}

void Emit(const std::string &event, const std::string &fields = {})
{
	std::string line = "{\"event\":\"" + JsonEscape(event) + "\",\"ts\":" + std::to_string(NowMillis());
	if (!fields.empty()) {
		line += ",";
		line += fields;
	}
	line += "}";
	std::fprintf(stdout, "%s\n", line.c_str());
	std::fflush(stdout);
#ifdef __WIIU__
	// stdout is not visible on a console; OSReport shows up as [OSConsole] in
	// Cemu's log and over a network logger on real hardware.
	OSReport("NETPEER: %s\n", line.c_str());
#endif
	if (g_resultFile != nullptr) {
		std::fprintf(g_resultFile, "%s\n", line.c_str());
		std::fflush(g_resultFile);
	}
}

std::string JsonStr(const std::string &value)
{
	return "\"" + JsonEscape(value) + "\"";
}

std::string JsonBool(bool value)
{
	return value ? "true" : "false";
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
	std::string role; // "host" or "join"
	int players = 2;
	unsigned short port = 6112;
	std::string bindAddress = "0.0.0.0";
	std::string connectAddress;
	int rounds = 1024;
	std::optional<std::string> password; // nullopt = public, otherwise (even empty) encrypted
	std::string scenario = "public";
	int deadlineSeconds = 60;
	int dropAfterRounds = -1;
	bool expectDrop = false;
	std::string probe;
	std::string configPath;
	std::string resultPath;
};

std::string DefaultResultPath(const std::string &configPath)
{
	if (!configPath.empty()) {
		const size_t slash = configPath.find_last_of('/');
		if (slash != std::string::npos)
			return configPath.substr(0, slash + 1) + "network_peer_result.jsonl";
	}
	return "network_peer_result.jsonl";
}

bool ReadConfigFile(const std::string &path, Config &config)
{
	std::FILE *file = std::fopen(path.c_str(), "r");
	if (file == nullptr)
		return false;

	char lineBuffer[512];
	while (std::fgets(lineBuffer, sizeof(lineBuffer), file) != nullptr) {
		std::string line = lineBuffer;
		const size_t comment = line.find('#');
		if (comment != std::string::npos)
			line.resize(comment);
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();
		size_t start = 0;
		while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
			++start;
		if (start >= line.size())
			continue;

		const size_t eq = line.find('=', start);
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(start, eq - start);
		std::string value = line.substr(eq + 1);
		while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
			key.pop_back();
		const size_t valueStart = value.find_first_not_of(" \t");
		value = valueStart == std::string::npos ? std::string() : value.substr(valueStart);
		if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
			value = value.substr(1, value.size() - 2);

		if (key == "role") {
			config.role = value;
		} else if (key == "players") {
			config.players = std::atoi(value.c_str());
		} else if (key == "port") {
			config.port = static_cast<unsigned short>(std::atoi(value.c_str()));
		} else if (key == "bind") {
			config.bindAddress = value;
		} else if (key == "connect") {
			config.connectAddress = value;
		} else if (key == "rounds") {
			config.rounds = std::atoi(value.c_str());
		} else if (key == "password") {
			if (value == "-")
				config.password = std::nullopt;
			else
				config.password = value;
		} else if (key == "scenario") {
			config.scenario = value;
		} else if (key == "deadline") {
			config.deadlineSeconds = std::atoi(value.c_str());
		} else if (key == "drop_after") {
			config.dropAfterRounds = std::atoi(value.c_str());
		} else if (key == "result") {
			config.resultPath = value;
		} else if (key == "expect_drop") {
			config.expectDrop = std::atoi(value.c_str()) != 0;
		} else if (key == "probe") {
			config.probe = value;
		}
	}
	std::fclose(file);
	return true;
}

[[noreturn]] void Usage()
{
	std::fprintf(stderr,
	    "usage: network_peer [--config=PATH] [--role=host|join] [--players=N]\n"
	    "                    [--port=PORT] [--bind=ADDR] [--connect=HOST:PORT]\n"
	    "                    [--rounds=N] [--password=PW|-] [--scenario=NAME]\n"
	    "                    [--deadline=SECONDS] [--drop-after=N] [--expect-drop] [--result=PATH]\n"
	    "defaults to ./network_peer.ini, then\n"
	    "/vol/external01/wiiu/apps/devilutionx/network_peer.ini on Wii U\n");
	std::exit(2);
}

void ApplyArgument(Config &config, const std::string &key, const std::string &value)
{
	if (key == "role") {
		config.role = value;
	} else if (key == "players") {
		config.players = std::atoi(value.c_str());
	} else if (key == "port") {
		config.port = static_cast<unsigned short>(std::atoi(value.c_str()));
	} else if (key == "bind") {
		config.bindAddress = value;
	} else if (key == "connect") {
		config.connectAddress = value;
	} else if (key == "rounds") {
		config.rounds = std::atoi(value.c_str());
	} else if (key == "password") {
		if (value == "-")
			config.password = std::nullopt;
		else
			config.password = value;
	} else if (key == "scenario") {
		config.scenario = value;
	} else if (key == "deadline") {
		config.deadlineSeconds = std::atoi(value.c_str());
	} else if (key == "drop-after") {
		config.dropAfterRounds = std::atoi(value.c_str());
	} else if (key == "result") {
		config.resultPath = value;
	} else if (key == "probe") {
		config.probe = value;
	} else if (key == "expect-drop") {
		config.expectDrop = value != "0" && value != "false";
	} else {
		Usage();
	}
}

// ---------------------------------------------------------------------------
// Application-level test messages
// ---------------------------------------------------------------------------

constexpr unsigned char MessageMagic[4] = { 'D', 'V', 'L', 'N' };
constexpr uint8_t MessageVersion = 1;
constexpr size_t MessageHeaderSize = 12; // magic + version + kind + sender + session + seq + length
constexpr size_t MessageChecksumSize = 4;
constexpr size_t MaxTestPayload = 128;
/// Unicast data messages carry this bit in the sequence number.
constexpr uint16_t UnicastFlag = 0x8000;

enum class MessageKind : uint8_t {
	Hello = 0,
	Go = 1,
	Data = 2,
	Ack = 3,
	Bye = 4,
};

uint8_t SessionTag(const std::string &scenario)
{
	uint32_t hash = 2166136261u;
	for (const char c : scenario) {
		hash ^= static_cast<unsigned char>(c);
		hash *= 16777619u;
	}
	return static_cast<uint8_t>(hash ^ 0x5A);
}

uint32_t Checksum(const std::vector<unsigned char> &bytes, size_t size)
{
	uint32_t hash = 2166136261u;
	for (size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

std::vector<unsigned char> MakeMessage(MessageKind kind, uint8_t sender, uint8_t session, uint16_t seq, const std::vector<unsigned char> &body)
{
	std::vector<unsigned char> out;
	out.reserve(MessageHeaderSize + body.size() + MessageChecksumSize);
	out.insert(out.end(), std::begin(MessageMagic), std::end(MessageMagic));
	out.push_back(MessageVersion);
	out.push_back(static_cast<unsigned char>(kind));
	out.push_back(sender);
	out.push_back(session);
	out.push_back(static_cast<unsigned char>(seq & 0xFF));
	out.push_back(static_cast<unsigned char>(seq >> 8));
	out.push_back(static_cast<unsigned char>(body.size() & 0xFF));
	out.push_back(static_cast<unsigned char>(body.size() >> 8));
	out.insert(out.end(), body.begin(), body.end());
	const uint32_t checksum = Checksum(out, out.size());
	out.push_back(static_cast<unsigned char>(checksum & 0xFF));
	out.push_back(static_cast<unsigned char>((checksum >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>((checksum >> 16) & 0xFF));
	out.push_back(static_cast<unsigned char>((checksum >> 24) & 0xFF));
	return out;
}

struct DecodedMessage {
	MessageKind kind;
	uint8_t sender;
	uint8_t session;
	uint16_t seq;
	std::vector<unsigned char> body;
};

bool DecodeMessage(const std::vector<unsigned char> &bytes, DecodedMessage &out, std::string &error)
{
	if (bytes.size() < MessageHeaderSize + MessageChecksumSize) {
		error = "short message";
		return false;
	}
	if (std::memcmp(bytes.data(), MessageMagic, sizeof(MessageMagic)) != 0) {
		error = "bad magic";
		return false;
	}
	if (bytes[4] != MessageVersion) {
		error = "bad version";
		return false;
	}
	out.kind = static_cast<MessageKind>(bytes[5]);
	out.sender = bytes[6];
	out.session = bytes[7];
	out.seq = static_cast<uint16_t>(bytes[8] | (bytes[9] << 8));
	const uint16_t length = static_cast<uint16_t>(bytes[10] | (bytes[11] << 8));
	if (length > MaxTestPayload || MessageHeaderSize + length + MessageChecksumSize != bytes.size()) {
		error = "bad length";
		return false;
	}
	const uint32_t expected = Checksum(bytes, MessageHeaderSize + length);
	const uint32_t actual = static_cast<uint32_t>(bytes[MessageHeaderSize + length])
	    | (static_cast<uint32_t>(bytes[MessageHeaderSize + length + 1]) << 8)
	    | (static_cast<uint32_t>(bytes[MessageHeaderSize + length + 2]) << 16)
	    | (static_cast<uint32_t>(bytes[MessageHeaderSize + length + 3]) << 24);
	if (actual != expected) {
		error = "bad checksum";
		return false;
	}
	out.body.assign(bytes.begin() + MessageHeaderSize, bytes.begin() + MessageHeaderSize + length);
	return true;
}

std::vector<unsigned char> MakePattern(uint8_t sender, uint16_t seq, size_t size)
{
	std::vector<unsigned char> pattern(size);
	uint32_t state = 0x9E3779B9u ^ (static_cast<uint32_t>(sender) << 24) ^ seq;
	for (size_t i = 0; i < size; i++) {
		state = state * 1664525u + 1013904223u;
		pattern[i] = static_cast<unsigned char>(state >> 24);
	}
	return pattern;
}

std::string BuildInfo()
{
	std::string info;
	info += "bits=" + std::to_string(static_cast<int>(sizeof(void *) * 8));
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	info += " be=1";
#else
	info += " be=0";
#endif
	info += " sdl=" + std::string(SDL_GetRevision());
	return info;
}

// ---------------------------------------------------------------------------
// Net event handling
// ---------------------------------------------------------------------------

struct NetEvents {
	int createEvents = 0;
	bool gameDataSeen = false;
	bool gameDataMatches = false;
	std::string eventError;
	std::vector<std::pair<uint8_t, net::leaveinfo_t>> leaves;
};

NetEvents g_events;

constexpr uint32_t FixtureSeed[4] = { 0x0000C0DE, 0x12345678, 0x9ABCDEF0, 0x0BADF00D };

void NetEventHandler(_SNETEVENT *event)
{
	switch (event->eventid) {
	case EVENT_TYPE_PLAYER_CREATE_GAME: {
		if (event->databytes < sizeof(GameData)) {
			g_events.eventError = "create-game payload smaller than GameData";
			return;
		}
		GameData gameData {};
		std::memcpy(&gameData, event->data, sizeof(gameData));
		SwapGameDataLE(gameData);
		if (gameData.size != sizeof(GameData)) {
			g_events.eventError = "create-game payload has a bad size field";
			return;
		}
		g_events.gameDataSeen = true;
		g_events.gameDataMatches = std::equal(std::begin(FixtureSeed), std::end(FixtureSeed), std::begin(gameData.gameSeed));
		g_events.createEvents++;
	} break;
	case EVENT_TYPE_PLAYER_LEAVE_GAME: {
		// _SNETEVENT::data is little-endian (storm_net.hpp); convert to native.
		uint32_t reasonRaw = 0;
		if (event->data != nullptr && event->databytes >= sizeof(reasonRaw))
			std::memcpy(&reasonRaw, event->data, sizeof(reasonRaw));
		reasonRaw = Swap32LE(reasonRaw);
		g_events.leaves.emplace_back(static_cast<uint8_t>(event->playerid), static_cast<net::leaveinfo_t>(reasonRaw));
	} break;
	case EVENT_TYPE_PLAYER_MESSAGE:
		break;
	}
}

std::string DescribeLeave(net::leaveinfo_t reason)
{
	switch (reason) {
	case net::leaveinfo_t::LEAVE_EXIT:
		return "EXIT";
	case net::leaveinfo_t::LEAVE_ENDING:
		return "ENDING";
	case net::leaveinfo_t::LEAVE_DROP:
		return "DROP";
	}
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<uint32_t>(reason));
	return buf;
}

// ---------------------------------------------------------------------------
// T0 socket probe: raw POSIX calls, the same sequence asio performs for the
// TCP server. Isolates platform socket gaps from dvlnet/engine behaviour.
// ---------------------------------------------------------------------------

std::string ProbeStep(const char *name, int result)
{
	const int error = errno;
	std::string fields = std::string("\"step\":\"") + name + "\",\"result\":" + std::to_string(result)
	    + ",\"errno\":" + std::to_string(error) + ",\"error\":" + JsonStr(std::strerror(error));
	Emit("PROBE_STEP", fields);
	return fields;
}

int RunSocketProbe(const Config &config)
{
	Emit("PROBE_START", "\"port\":" + std::to_string(config.port));

	int listener = socket(AF_INET, SOCK_STREAM, 0);
	ProbeStep("socket", listener);
	if (listener < 0)
		return 1;

	int one = 1;
	ProbeStep("setsockopt(SO_REUSEADDR)", setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)));

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(config.bindAddress.c_str());
	address.sin_port = htons(config.port);
	Emit("PROBE_BIND", "\"address\":" + JsonStr(config.bindAddress) + ",\"port\":" + std::to_string(config.port));
	ProbeStep("bind", bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)));
	ProbeStep("listen", listen(listener, 8));

	sockaddr_in local {};
	socklen_t localLength = sizeof(local);
	ProbeStep("getsockname", getsockname(listener, reinterpret_cast<sockaddr *>(&local), &localLength));
	if (local.sin_family == AF_INET)
		Emit("PROBE_LOCAL", "\"port\":" + std::to_string(ntohs(local.sin_port)));

	const int flags = fcntl(listener, F_GETFL, 0);
	ProbeStep("fcntl(F_GETFL)", flags);
	if (flags >= 0)
		ProbeStep("fcntl(F_SETFL,O_NONBLOCK)", fcntl(listener, F_SETFL, flags | O_NONBLOCK));

	sockaddr_in clientAddress {};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	clientAddress.sin_port = htons(config.port);

	int client = socket(AF_INET, SOCK_STREAM, 0);
	ProbeStep("socket(client)", client);
	if (client >= 0)
		ProbeStep("connect(loopback)", connect(client, reinterpret_cast<sockaddr *>(&clientAddress), sizeof(clientAddress)));

	int accepted = -1;
	for (int attempt = 0; attempt < 100; attempt++) {
		sockaddr_in peer {};
		socklen_t peerLength = sizeof(peer);
		accepted = accept(listener, reinterpret_cast<sockaddr *>(&peer), &peerLength);
		if (accepted >= 0)
			break;
		if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINPROGRESS)
			break;
		SDL_Delay(10);
	}
	ProbeStep("accept", accepted);

	if (accepted >= 0 && client >= 0) {
		const char payload[] = "probe";
		ProbeStep("send", static_cast<int>(send(client, payload, sizeof(payload), 0)));
		char buffer[8] {};
		ProbeStep("recv", static_cast<int>(recv(accepted, buffer, sizeof(buffer), 0)));
	}

	if (accepted >= 0)
		close(accepted);
	if (client >= 0)
		close(client);
	close(listener);
	Emit("RESULT", "\"status\":\"PASS\"");
	return 0;
}

// ---------------------------------------------------------------------------
// Peer state machine
// ---------------------------------------------------------------------------

struct Peer {
	Config config;
	uint8_t id = 0xFF;
	uint8_t session = 0;
	bool isHost = false;
	int playerId = -1;

	std::array<bool, MAX_PLRS> connected {};
	std::array<bool, MAX_PLRS> heardHello {};
	std::array<bool, MAX_PLRS> seenGo {};
	std::array<bool, MAX_PLRS> seenBroadcast {};
	std::array<bool, MAX_PLRS> seenUnicast {}; // sender sent a unicast to us
	std::array<bool, MAX_PLRS> active {}; // currently connected, per the last SNetReceiveTurns
	std::array<bool, MAX_PLRS> dropped {};
	bool sawDrop = false;
	std::array<bool, MAX_PLRS> seenBye {};
	std::array<bool, MAX_PLRS> seenAck {};
	uint32_t unexpectedUnicast = 0;
	uint32_t protocolErrors = 0;
	std::string firstProtocolError;
	std::string lastError;
	std::array<int32_t, MAX_PLRS> lastTurn {};
	uint32_t turnErrors = 0;

	uint32_t deadlineAt = 0;

	bool Expired() const
	{
		return static_cast<int32_t>(NowMillis() - deadlineAt) >= 0;
	}

	void SetDeadlineSeconds(int seconds)
	{
		deadlineAt = NowMillis() + static_cast<uint32_t>(seconds) * 1000u;
	}

	bool AllPeers(const std::array<bool, MAX_PLRS> &flags) const
	{
		for (int i = 0; i < config.players; i++) {
			if (static_cast<uint8_t>(i) != id && !flags[i])
				return false;
		}
		return true;
	}

	/// Like AllPeers(), but only for peers that are still connected.
	bool AllActiveHave(const std::array<bool, MAX_PLRS> &flags) const
	{
		for (int i = 0; i < config.players; i++) {
			if (static_cast<uint8_t>(i) == id || !active[i])
				continue;
			if (!flags[i])
				return false;
		}
		return true;
	}

	bool AllConnected() const
	{
		for (int i = 0; i < config.players; i++) {
			if (!connected[i])
				return false;
		}
		return true;
	}

	std::string PlayerListMissing(const std::array<bool, MAX_PLRS> &flags) const
	{
		std::string out = "[";
		bool first = true;
		for (int i = 0; i < config.players; i++) {
			if (static_cast<uint8_t>(i) == id || flags[i])
				continue;
			if (!first)
				out += ",";
			first = false;
			out += std::to_string(i);
		}
		out += "]";
		return out;
	}

	std::string PlayerList(const std::array<bool, MAX_PLRS> &flags) const
	{
		std::string out = "[";
		bool first = true;
		for (int i = 0; i < MAX_PLRS; i++) {
			if (!flags[i])
				continue;
			if (!first)
				out += ",";
			first = false;
			out += std::to_string(i);
		}
		out += "]";
		return out;
	}

	void RecordProtocolError(const std::string &error)
	{
		if (protocolErrors++ == 0)
			firstProtocolError = error;
	}

	void HandleMessage(const std::vector<unsigned char> &bytes)
	{
		DecodedMessage message {};
		std::string error;
		if (!DecodeMessage(bytes, message, error)) {
			RecordProtocolError(error);
			return;
		}
		if (message.session != session) {
			RecordProtocolError("session mismatch");
			return;
		}
		if (message.sender >= MAX_PLRS) {
			RecordProtocolError("sender id out of range");
			return;
		}
		if (message.sender == id) {
			RecordProtocolError("received own message");
			return;
		}
		const bool unicast = (message.seq & UnicastFlag) != 0;
		const uint16_t payloadSeq = message.seq & ~UnicastFlag;

		switch (message.kind) {
		case MessageKind::Hello:
			heardHello[message.sender] = true;
			break;
		case MessageKind::Go:
			seenGo[message.sender] = true;
			break;
		case MessageKind::Data: {
			if (message.body.empty()) {
				RecordProtocolError("empty data payload");
				return;
			}
			const size_t expectedSize = message.body[0];
			if (message.body.size() != expectedSize + 1) {
				RecordProtocolError("data payload length mismatch");
				return;
			}
			const std::vector<unsigned char> expected = MakePattern(message.sender, payloadSeq, expectedSize);
			if (!std::equal(expected.begin(), expected.end(), message.body.begin() + 1)) {
				RecordProtocolError("data payload corrupted");
				return;
			}
			if (unicast) {
				if (payloadSeq == id)
					seenUnicast[message.sender] = true;
				else
					unexpectedUnicast++;
			} else {
				seenBroadcast[message.sender] = true;
			}
		} break;
		case MessageKind::Bye:
			seenBye[message.sender] = true;
			break;
		case MessageKind::Ack:
			seenAck[message.sender] = true;
			break;
		default:
			RecordProtocolError("unknown message kind");
			break;
		}
	}

	void PumpMessages()
	{
		while (true) {
			uint8_t sender = 0;
			void *data = nullptr;
			size_t size = 0;
			if (!SNetReceiveMessage(&sender, &data, &size))
				break;
			if (data == nullptr || size == 0)
				continue;
			const auto *bytes = static_cast<const unsigned char *>(data);
			HandleMessage(std::vector<unsigned char>(bytes, bytes + size));
		}
	}

	/**
	 * @return true if SNetReceiveTurns reported that every connected peer
	 * delivered its turn for the current sequence number.
	 */
	bool PumpTurns()
	{
		char *data[MAX_PLRS] {};
		size_t sizes[MAX_PLRS] {};
		uint32_t status[MAX_PLRS] {};
		const bool complete = SNetReceiveTurns(MAX_PLRS, data, sizes, status);

		for (int i = 0; i < MAX_PLRS; i++) {
			if ((status[i] & PS_CONNECTED) != 0) {
				connected[i] = true;
				active[i] = true;
			} else if (active[i]) {
				// The provider dropped this peer (voluntary leave or timeout).
				active[i] = false;
				dropped[i] = true;
				sawDrop = true;
			}
		}
		if (complete) {
			for (int i = 0; i < config.players; i++) {
				if (static_cast<uint8_t>(i) == id || !active[i])
					continue;
				if ((status[i] & PS_TURN_ARRIVED) == 0 || sizes[i] != sizeof(int32_t)) {
					turnErrors++;
					continue;
				}
				int32_t value = 0;
				std::memcpy(&value, data[i], sizeof(value));
				lastTurn[i] = value;
			}
		}
		return complete;
	}

	void Pump()
	{
		PumpMessages();
		PumpTurns();
	}

	void SendMessage(MessageKind kind, const std::vector<unsigned char> &body, int destination = SNPLAYER_OTHERS, uint16_t seq = 0)
	{
		std::vector<unsigned char> message = MakeMessage(kind, id, session, seq, body);
		if (!SNetSendMessage(static_cast<uint8_t>(destination), message.data(), message.size()))
			RecordProtocolError("SNetSendMessage failed");
	}

	void SendData(int destination, uint16_t seqWithFlag, size_t size)
	{
		std::vector<unsigned char> body(1);
		body[0] = static_cast<unsigned char>(size);
		const std::vector<unsigned char> pattern = MakePattern(id, seqWithFlag & ~UnicastFlag, size);
		body.insert(body.end(), pattern.begin(), pattern.end());
		SendMessage(MessageKind::Data, body, destination, seqWithFlag);
	}

	bool WaitUntilRoster()
	{
		while (!Expired()) {
			Pump();
			if (AllConnected()) {
				Emit("ROSTER", "\"players\":" + std::to_string(config.players));
				return true;
			}
			SDL_Delay(2);
		}
		lastError = "roster did not complete: " + PlayerList(connected);
		return false;
	}

	bool WaitForFlag(const char *phase, MessageKind sendKind, const std::array<bool, MAX_PLRS> &flags)
	{
		// Announce at least once before checking: a peer that already collected
		// our earlier announcement (sent while it was still in the previous
		// phase) would otherwise proceed without us ever having announced.
		SendMessage(sendKind, {});
		while (!Expired()) {
			Pump();
			if (AllPeers(flags))
				return true;
			SendMessage(sendKind, {});
			SDL_Delay(20);
		}
		lastError = std::string(phase) + " timed out, missing peers: " + PlayerListMissing(flags);
		return false;
	}

	int32_t EncodeTurn(int round, int player) const
	{
		return static_cast<int32_t>((static_cast<uint32_t>(round) << 6) | (static_cast<uint32_t>(player) << 2) | 1u);
	}
};

char *PasswordPtr(const Config &config)
{
	if (!config.password.has_value())
		return nullptr;
	// SNetCreateGame/SNetJoinGame copy the string before returning.
	return const_cast<char *>(config.password->c_str());
}

bool RunPeer(Peer &peer)
{
	const Config &config = peer.config;
	peer.session = SessionTag(config.scenario);

	HeadlessMode = true;
	gbIsMultiplayer = true;
	gbIsSpawn = false;
	provider = SELCONN_TCP;
	IsLoopback = false;
	Players.resize(MAX_PLRS);

#ifdef __WIIU__
	wiiu_enable_network();
#ifdef PACKET_ENCRYPTION
	// libsodium's cookie generation needs an RNG before the first packet.
	randombytes_wiiurandom_init();
#endif
#endif

	if (SDL_Init(SDL_INIT_TIMER) != 0) {
		peer.lastError = std::string("SDL_Init failed: ") + SDL_GetError();
		return false;
	}
	peer.SetDeadlineSeconds(config.deadlineSeconds);

	if (!config.configPath.empty())
		Emit("CONFIG", "\"path\":" + JsonStr(config.configPath));

	Emit("BOOT",
	    "\"role\":" + JsonStr(config.role)
	        + ",\"players\":" + std::to_string(config.players)
	        + ",\"port\":" + std::to_string(config.port)
	        + ",\"rounds\":" + std::to_string(config.rounds)
	        + ",\"scenario\":" + JsonStr(config.scenario)
	        + ",\"password\":"
	        + (config.password.has_value() ? JsonStr(config.password->empty() ? "<empty>" : "<set>") : std::string("null"))
	        + ",\"build\":" + JsonStr(std::string(__DATE__) + " " + __TIME__)
	        + ",\"peer\":" + JsonStr(BuildInfo())
#ifdef __WIIU__
	        + ",\"rng\":" + JsonStr(wiiu_random_source())
#endif
	);

	GameData gameData {};
	gameData.size = sizeof(GameData);
	gameData.isSpawn = 0;
	gameData.programid = 0;
	gameData.versionMajor = PROJECT_VERSION_MAJOR;
	gameData.versionMinor = PROJECT_VERSION_MINOR;
	gameData.versionPatch = PROJECT_VERSION_PATCH;
	gameData.nDifficulty = DIFF_NORMAL;
	gameData.nTickRate = 20;
	gameData.bRunInTown = 1;
	gameData.bTheoQuest = 0;
	gameData.bCowQuest = 0;
	gameData.bFriendlyFire = 0;
	gameData.fullQuests = 1;
	std::copy(std::begin(FixtureSeed), std::end(FixtureSeed), std::begin(gameData.gameSeed));
	sgGameInitInfo = gameData;

	if (!SNetInitializeProvider(SELCONN_TCP, &gameData)) {
		peer.lastError = "SNetInitializeProvider failed";
		return false;
	}
	SNetRegisterEventHandler(EVENT_TYPE_PLAYER_CREATE_GAME, NetEventHandler);
	SNetRegisterEventHandler(EVENT_TYPE_PLAYER_LEAVE_GAME, NetEventHandler);

	GetOptions().Network.port.SetValue(config.port);

	const bool isHost = config.role == "host";
	if (isHost) {
		GameData wireData = gameData;
		SwapGameDataLE(wireData);
		std::string bindAddress = config.bindAddress;
		peer.playerId = -1;
		if (!SNetCreateGame(bindAddress.c_str(), PasswordPtr(config),
		        reinterpret_cast<char *>(&wireData), sizeof(wireData), &peer.playerId)) {
			peer.lastError = std::string("SNetCreateGame failed (self-join on ")
			    + config.bindAddress + ":" + std::to_string(config.port) + "): " + SDL_GetError();
			return false;
		}
		Emit("HOST_READY",
		    "\"player_id\":" + std::to_string(peer.playerId)
		        + ",\"bind\":" + JsonStr(config.bindAddress)
		        + ",\"port\":" + std::to_string(config.port)
		        + ",\"encrypted\":" + JsonBool(config.password.has_value()));
	} else {
		std::string endpoint = config.connectAddress;
		peer.playerId = -1;
		if (!SNetJoinGame(endpoint.data(), PasswordPtr(config), &peer.playerId)) {
			peer.lastError = std::string("SNetJoinGame failed (") + config.connectAddress + "): " + SDL_GetError();
			return false;
		}
		Emit("JOINED",
		    "\"player_id\":" + std::to_string(peer.playerId)
		        + ",\"endpoint\":" + JsonStr(config.connectAddress)
		        + ",\"encrypted\":" + JsonBool(config.password.has_value()));
	}

	peer.isHost = isHost;
	peer.id = static_cast<uint8_t>(peer.playerId);
	MyPlayerId = peer.id;
	MyPlayer = &Players[peer.id];

	if (!peer.WaitUntilRoster())
		return false;

	if (!isHost) {
		if (!g_events.gameDataSeen) {
			peer.lastError = "host game data event was not delivered";
			return false;
		}
		if (!g_events.gameDataMatches) {
			peer.lastError = "host game data (seed) did not survive the wire";
			return false;
		}
		Emit("GAME_DATA", "\"matches\":true");
	}

	// -- message channel: HELLO roster, then an application barrier ----------
	{
		const uint32_t start = NowMillis();
		if (!peer.WaitForFlag("hello", MessageKind::Hello, peer.heardHello))
			return false;
		Emit("HELLO_DONE", "\"ms\":" + std::to_string(NowMillis() - start));

		if (!peer.WaitForFlag("go", MessageKind::Go, peer.seenGo))
			return false;
		Emit("BARRIER_DONE", "\"ms\":" + std::to_string(NowMillis() - start));
	}

	if (!g_events.eventError.empty()) {
		peer.lastError = g_events.eventError;
		return false;
	}

	// -- message test: broadcast and unicast, exact bytes --------------------
	{
		const uint32_t errorsBefore = peer.protocolErrors;

		peer.SendData(SNPLAYER_OTHERS, 0x4242, 64);
		for (int i = 0; i < config.players; i++) {
			if (static_cast<uint8_t>(i) == peer.id)
				continue;
			peer.SendData(i, static_cast<uint16_t>(UnicastFlag | i), 32);
		}

		const uint32_t start = NowMillis();
		while (!peer.Expired()) {
			peer.Pump();
			if (peer.AllPeers(peer.seenBroadcast) && peer.AllPeers(peer.seenUnicast) && peer.unexpectedUnicast == 0)
				break;
			SDL_Delay(5);
		}
		const bool ok = peer.AllPeers(peer.seenBroadcast) && peer.AllPeers(peer.seenUnicast)
		    && peer.unexpectedUnicast == 0 && peer.protocolErrors == errorsBefore;
		Emit("MESSAGE_TEST",
		    "\"ok\":" + JsonBool(ok)
		        + ",\"broadcast\":" + JsonBool(peer.AllPeers(peer.seenBroadcast))
		        + ",\"unicast\":" + JsonBool(peer.AllPeers(peer.seenUnicast))
		        + ",\"unexpected_unicast\":" + std::to_string(peer.unexpectedUnicast)
		        + ",\"protocol_errors\":" + std::to_string(peer.protocolErrors)
		        + ",\"ms\":" + std::to_string(NowMillis() - start));
		if (!ok) {
			peer.lastError = "message test failed";
			return false;
		}
	}

	// -- turn test: sequence wrap, native int32 values ----------------------
	{
		const uint32_t start = NowMillis();
		int completed = 0;
		for (int round = 0; round < config.rounds; round++) {
			int32_t turnValue = peer.EncodeTurn(round, peer.id);
			if (!SNetSendTurn(reinterpret_cast<char *>(&turnValue), sizeof(turnValue))) {
				peer.lastError = "SNetSendTurn failed at round " + std::to_string(round);
				return false;
			}
			bool gotTurn = false;
			const uint32_t roundDeadline = NowMillis() + 10000;
			while (static_cast<int32_t>(NowMillis() - roundDeadline) < 0) {
				if (peer.PumpTurns()) {
					gotTurn = true;
					break;
				}
				if (peer.Expired()) {
					peer.lastError = "turn deadline while waiting for round " + std::to_string(round);
					return false;
				}
				SDL_Delay(1);
			}
			if (!gotTurn) {
				peer.lastError = "missing turns for round " + std::to_string(round);
				return false;
			}
			for (int i = 0; i < config.players; i++) {
				if (static_cast<uint8_t>(i) == peer.id || !peer.active[i])
					continue;
				if (peer.lastTurn[i] != peer.EncodeTurn(round, i)) {
					peer.turnErrors++;
					peer.lastError = "turn value mismatch from player " + std::to_string(i)
					    + " at round " + std::to_string(round);
				}
			}
			completed++;

			if (peer.sawDrop) {
				Emit("PEER_LEFT", std::string("\"round\":") + std::to_string(round));
				break;
			}

			if (config.dropAfterRounds >= 0 && completed >= config.dropAfterRounds) {
				// Simulates a peer that disappears without a graceful disconnect.
				Emit("DROPPING", "\"after_rounds\":" + std::to_string(completed));
				std::fflush(stdout);
				std::abort();
			}
		}
		Emit("TURN_TEST",
		    "\"rounds\":" + std::to_string(completed)
		        + ",\"dropped\":" + JsonBool(peer.sawDrop)
		        + ",\"value_errors\":" + std::to_string(peer.turnErrors)
		        + ",\"ms\":" + std::to_string(NowMillis() - start));
		if (peer.turnErrors != 0)
			return false;
	}

	// -- graceful shutdown ---------------------------------------------------
	if (!isHost) {
		// Make sure the host has processed everything before the socket closes.
		while (!peer.Expired() && !peer.seenAck[0]) {
			peer.SendMessage(MessageKind::Bye, {});
			peer.Pump();
			SDL_Delay(20);
		}
		const bool acked = peer.seenAck[0];
		Emit("BYE", "\"acked\":" + JsonBool(acked));
		if (!acked) {
			peer.lastError = "host did not acknowledge the goodbye";
			return false;
		}
	} else {
		const uint32_t waitDeadline = NowMillis() + 15000;
		while (static_cast<int32_t>(NowMillis() - waitDeadline) < 0 && !peer.AllActiveHave(peer.seenBye)) {
			peer.PumpMessages();
			for (int i = 0; i < peer.config.players; i++) {
				if (static_cast<uint8_t>(i) != peer.id && peer.seenBye[i])
					peer.SendMessage(MessageKind::Ack, {}, i, static_cast<uint16_t>(i));
			}
			SDL_Delay(10);
		}
		Emit("BYE", "\"acked\":" + JsonBool(peer.AllActiveHave(peer.seenBye)));
		if (!peer.AllActiveHave(peer.seenBye)) {
			peer.lastError = "clients did not say goodbye";
			return false;
		}
	}

	if (isHost) {
		// Every client must produce a LEAVE_EXIT event through the provider.
		const uint32_t leaveDeadline = NowMillis() + 15000;
		size_t expectedLeaves = static_cast<size_t>(config.players - 1);
		while (static_cast<int32_t>(NowMillis() - leaveDeadline) < 0) {
			peer.Pump();
			size_t count = 0;
			for (const auto &[player, reason] : g_events.leaves) {
				if (player != peer.id && static_cast<int>(player) < config.players)
					count++;
			}
			if (count >= expectedLeaves)
				break;
			SDL_Delay(5);
		}
	}

	std::string leaveReport;
	bool leavesOk = true;
	bool sawExpectedDrop = false;
	for (const auto &[player, reason] : g_events.leaves) {
		if (player == peer.id || static_cast<int>(player) >= config.players)
			continue;
		if (!leaveReport.empty())
			leaveReport += ",";
		leaveReport += "{\"player\":" + std::to_string(player) + ",\"reason\":" + JsonStr(DescribeLeave(reason)) + "}";
		if (reason == net::leaveinfo_t::LEAVE_DROP)
			sawExpectedDrop = true;
		if (config.expectDrop ? (reason != net::leaveinfo_t::LEAVE_EXIT && reason != net::leaveinfo_t::LEAVE_DROP)
		                      : (reason != net::leaveinfo_t::LEAVE_EXIT))
			leavesOk = false;
	}
	Emit("LEAVE_EVENTS", "\"events\":[" + leaveReport + "],\"ok\":" + JsonBool(leavesOk));
	if (isHost && (!leavesOk || leaveReport.empty())) {
		peer.lastError = "clients did not leave cleanly (expected LEAVE_EXIT)";
		return false;
	}
	if (config.expectDrop && !sawExpectedDrop) {
		peer.lastError = "expected a LEAVE_DROP event, none was delivered";
		return false;
	}

	SNetLeaveGame(net::leaveinfo_t::LEAVE_EXIT);
	SDL_Delay(100);
	SNetUnregisterEventHandler(EVENT_TYPE_PLAYER_CREATE_GAME);
	SNetUnregisterEventHandler(EVENT_TYPE_PLAYER_LEAVE_GAME);
	SNetDestroy();
	MyPlayer = nullptr;
	Players.clear();

	Emit("CLEANUP");
	if (peer.protocolErrors != 0) {
		peer.lastError = "protocol errors: " + std::to_string(peer.protocolErrors) + " (" + peer.firstProtocolError + ")";
		return false;
	}
	return true;
}

} // namespace
} // namespace devilution

int main(int argc, char **argv)
{
	using namespace devilution;

#ifdef __WIIU__
	OSReport("NETPEER: main() entered, argv=%d\n", argc);
#endif

	Config config;

	// 1. explicit --config, 2. env var, 3. local file, 4. Wii U data directory
	std::string configPath;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg.rfind("--config=", 0) == 0) {
			configPath = arg.substr(9);
		} else if (arg == "--config" && i + 1 < argc) {
			configPath = argv[++i];
		}
	}

	if (configPath.empty()) {
		const char *envPath = std::getenv("DEVILUTIONX_NETWORK_PEER_CONFIG");
		const std::string probe = envPath != nullptr ? envPath : "network_peer.ini";
		if (ReadConfigFile(probe, config)) {
			configPath = probe;
		} else {
			const std::string wiiuPath = "/vol/external01/wiiu/apps/devilutionx/network_peer.ini";
			if (ReadConfigFile(wiiuPath, config))
				configPath = wiiuPath;
		}
	} else if (!ReadConfigFile(configPath, config)) {
		std::fprintf(stderr, "cannot read config file: %s\n", configPath.c_str());
		return 2;
	}
	config.configPath = configPath;

	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg == "--config" && i + 1 < argc) {
			++i;
			continue;
		}
		if (arg.rfind("--", 0) != 0)
			continue;
		std::string keyValue = arg.substr(2);
		std::string value;
		const size_t eq = keyValue.find('=');
		if (eq != std::string::npos) {
			value = keyValue.substr(eq + 1);
			keyValue.resize(eq);
		} else if (i + 1 < argc) {
			value = argv[++i];
		} else {
			Usage();
		}
		ApplyArgument(config, keyValue, value);
	}

	if (config.resultPath.empty())
		config.resultPath = DefaultResultPath(configPath);
	g_resultFile = std::fopen(config.resultPath.c_str(), "w");
	if (g_resultFile == nullptr) {
		std::fprintf(stderr, "warning: cannot open result file: %s\n", config.resultPath.c_str());
#ifdef __WIIU__
		OSReport("NETPEER: warning: cannot open result file %s\n", config.resultPath.c_str());
#endif
	}

	if (config.probe == "socket") {
#ifdef __WIIU__
		wiiu_enable_network();
#endif
		if (SDL_Init(SDL_INIT_TIMER) != 0) {
			std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
			return 2;
		}
		const int probeResult = RunSocketProbe(config);
		if (g_resultFile != nullptr)
			std::fclose(g_resultFile);
		return probeResult;
	}

	if (config.role != "host" && config.role != "join") {
		std::fprintf(stderr, "role must be host or join\n");
		return 2;
	}
	if (config.players < 2 || config.players > MAX_PLRS) {
		std::fprintf(stderr, "players must be between 2 and %d\n", MAX_PLRS);
		return 2;
	}
	if (config.role == "join" && config.connectAddress.empty()) {
		std::fprintf(stderr, "join requires connect=HOST:PORT\n");
		return 2;
	}

	Peer peer;
	peer.config = config;

	const bool ok = RunPeer(peer);
	if (ok) {
		Emit("RESULT", "\"status\":\"PASS\"");
	} else {
		Emit("RESULT",
		    "\"status\":\"FAIL\",\"reason\":" + JsonStr(peer.lastError)
		        + ",\"protocol_errors\":" + std::to_string(peer.protocolErrors));
	}
	if (g_resultFile != nullptr) {
		std::fclose(g_resultFile);
		g_resultFile = nullptr;
	}
	return ok ? 0 : 1;
}
