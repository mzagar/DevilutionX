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
 *
 * Everything reported here is also written to `boot-diag.txt` on the SD card.
 * OSReport is not visible on real hardware without extra tooling, so that file
 * is the only way to find out what happened when a boot fails there.
 */
#include "platform/wiiu/init.hpp"
#include "platform/wiiu/network.hpp"
#ifdef PACKET_ENCRYPTION
#include "platform/wiiu/random.hpp"
#endif

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>

#ifdef __WIIU__
#include <coreinit/debug.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <nsysnet/_socket.h>
#include <sys/socket.h>
#include <sys/select.h>
#endif

#include "utils/paths.h"

namespace {

/// Read-only game data bundled into the WUHB (`wuhbtool --content`).
constexpr const char *ContentAssetsPath = "/vol/content/assets";
/// Writable directory on the SD card (created on first run).
///
/// The name is a build-time knob so two builds (for example the regular port
/// and the local co-op branch) can live side by side on one SD card with their
/// own config, saves and game data. See WIIU_APP_DIR_NAME.
#ifndef WIIU_APP_DIR_NAME
#define WIIU_APP_DIR_NAME "devilutionx"
#endif
constexpr const char *SdDataPath = "/vol/external01/wiiu/apps/" WIIU_APP_DIR_NAME;
/// Name of the SD-card copy of the boot log.
constexpr const char *DiagFileName = "boot-diag.txt";

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/// Collects everything reported during boot so it can be dumped to the SD card.
std::string &DiagBuffer()
{
	static std::string buffer;
	return buffer;
}

/// Reports a diagnostic line to OSReport *and* keeps it for `boot-diag.txt`.
///
/// Note that OSReport's printf is not a full C library: length modifiers such
/// as `%zu` and `%lu` are unsupported and swallow the rest of the line, so
/// always cast to `int` and use `%d`.
void Diag(const char *fmt, ...)
{
	char line[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

#ifdef __WIIU__
	OSReport("WIIUINIT: %s\n", line);
#endif

	DiagBuffer() += line;
	DiagBuffer() += '\n';
}

/// Writes the collected diagnostics to the SD card, so a failed boot can be
/// diagnosed without any logging hardware attached. Tries the data directory
/// first and falls back to the SD card root, which always exists.
void DiagFlushToSd()
{
	const std::string candidates[] = {
		std::string(SdDataPath) + "/" + DiagFileName,
		std::string("/vol/external01/") + DiagFileName,
	};

	for (const std::string &path : candidates) {
		FILE *file = fopen(path.c_str(), "wb");
		if (file == nullptr)
			continue;
		std::string content = DiagBuffer();
		content += "(this file: '";
		content += path;
		content += "')\n";
		const bool ok = fwrite(content.data(), 1, content.size(), file) == content.size();
		fclose(file);
		if (ok)
			return;
		remove(path.c_str());
	}

	Diag("could not write boot diagnostics to the SD card");
}

void DiagHeader()
{
	SDL_version version;
	SDL_GetVersion(&version);
	Diag("=== DevilutionX Wii U boot diagnostics ===");
	Diag("build: %s %s (init.cpp)", __DATE__, __TIME__);
	Diag("SDL: %d.%d.%d", version.major, version.minor, version.patch);
}

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
		Diag("could not create '%s'", path.c_str());
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

void LogExists(const char *label, const char *path)
{
	Diag("  %-28s '%s' -> %s", label, path, FileExists(path) ? "YES" : "no");
}

void ListDir(const char *path, int maxEntries = 16)
{
	DIR *dir = opendir(path);
	if (dir == nullptr) {
		Diag("  listdir '%s' -> FAILED", path);
		return;
	}
	int n = 0;
	while (struct dirent *ent = readdir(dir)) {
		if (++n > maxEntries) {
			Diag("  listdir '%s' -> ... (truncated at %d)", path, maxEntries);
			break;
		}
		Diag("    [%d] %s", static_cast<int>(ent->d_type), ent->d_name);
	}
	closedir(dir);
	if (n == 0)
		Diag("  listdir '%s' -> empty", path);
}

void LogEnvironment()
{
	Diag("=== Wii U environment ===");

	char cwd[512];
	Diag("  cwd = '%s'", getcwd(cwd, sizeof(cwd)) != nullptr ? cwd : "(FAILED)");

	char *base = SDL_GetBasePath();
	Diag("  SDL_GetBasePath = '%s'", base != nullptr ? base : "(null)");
	SDL_ClearError();
	if (base != nullptr)
		SDL_free(base);

	char *pref = SDL_GetPrefPath("diasurgical", "devilution");
	Diag("  SDL_GetPrefPath = '%s'", pref != nullptr ? pref : "(null)");
	SDL_ClearError();
	if (pref != nullptr)
		SDL_free(pref);

	Diag("  -- volumes --");
	LogExists("content mount", "/vol/content");
	LogExists("content/assets", ContentAssetsPath);
	LogExists("content/assets/spawn.mpq", "/vol/content/assets/spawn.mpq");
	LogExists("sd root", "/vol/external01");
	LogExists("sd data dir (ours)", SdDataPath);
	LogExists("save volume", "/vol/save");
	LogExists("cwd-relative assets", "assets");
	LogExists("cwd-relative spawn.mpq", "spawn.mpq");

	Diag("  -- listing /vol --");
	ListDir("/vol");
	Diag("  -- listing /vol/content --");
	ListDir("/vol/content");
	Diag("  -- listing /vol/external01 --");
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
		Diag("assets: using WUHB content mount '%s'", ContentAssetsPath);
	} else {
		// `paths::AssetsPath()` defaults to "assets/" and `paths::BasePath()` to "",
		// i.e. both are relative to the CWD, which is the SD card root for
		// plain SD-card builds. Nothing to do.
		Diag("assets: no content mount, using CWD-relative paths");
	}

	// --- writable data (config, saves, mods, screenshots) --------------------
	const std::string dataDir = SdDataPath;
	if (EnsureDir(dataDir) && IsWritableDir(dataDir)) {
		devilution::paths::SetPrefPath(dataDir);
		devilution::paths::SetConfigPath(dataDir);
		Diag("pref/config: using '%s'", dataDir.c_str());
	} else {
		Diag("pref/config: SD data dir unusable, using CWD-relative paths");
	}
}

// ---------------------------------------------------------------------------
// M0 network probe
//
// TEMPORARY scaffolding: proves out what wut/Cemu actually support before the
// TCP provider is wired in. Delete once network multiplayer works.
// ---------------------------------------------------------------------------

void ProbeStep(const char *what, int rc)
{
	Diag("  %-32s rc=%-4d errno=%d", what, rc, errno);
}

void ProbeNetworking()
{
	Diag("=== network probe ===");
	Diag("  FD_SETSIZE=%d NSYSNET_FD_SETSIZE=%d", FD_SETSIZE, NSYSNET_FD_SETSIZE);

	socket_lib_init();
	Diag("  socket_lib_init() returned (direct)");

	// --- Phase 1: DNS. If this fails, Cemu is not bridging to the host at all ---
	addrinfo hints {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *resolved = nullptr;
	int rc = getaddrinfo("example.com", "80", &hints, &resolved);
	Diag("  %-32s rc=%d errno=%d", "getaddrinfo(example.com)", rc, errno);
	if (resolved != nullptr) {
		char ip[32] = {};
		inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(resolved->ai_addr)->sin_addr, ip, sizeof(ip));
		Diag("  example.com -> %s", ip);
	}
	DiagFlushToSd();

	// --- Phase 2: outbound TCP. select() for writable proves a real handshake ---
	if (resolved != nullptr) {
		rc = socket(AF_INET, SOCK_STREAM, 0);
		ProbeStep("socket(outbound)", rc);
		const int out = rc;
		if (out >= 0) {
			rc = connect(out, resolved->ai_addr, resolved->ai_addrlen);
			ProbeStep("connect(example.com:80)", rc);

			fd_set wr;
			FD_ZERO(&wr);
			FD_SET(out, &wr);
			timeval timeout { 5, 0 };
			rc = select(out + 1, nullptr, &wr, nullptr, &timeout);
			ProbeStep("select(writable, 5s)", rc);

			if (rc > 0) {
				const char req[] = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
				rc = static_cast<int>(send(out, req, sizeof(req) - 1, 0));
				ProbeStep("send(HTTP request)", rc);

				fd_set rd;
				FD_ZERO(&rd);
				FD_SET(out, &rd);
				timeval readTimeout { 5, 0 };
				rc = select(out + 1, &rd, nullptr, nullptr, &readTimeout);
				ProbeStep("select(readable, 5s)", rc);

				if (rc > 0) {
					Diag("  --- recv on the REAL socket ---");
					DiagFlushToSd();
					char buf[128] = {};
					rc = static_cast<int>(recv(out, buf, sizeof(buf) - 1, 0));
					ProbeStep("recv(example.com)", rc);
					if (rc > 0) {
						buf[rc] = '\0';
						for (char *p = buf; *p != '\0'; ++p) {
							if (*p == '\n' || *p == '\r')
								*p = ' ';
						}
						Diag("  payload = '%s'", buf);
					}
				}
			}
			close(out);
		}
	}
	if (resolved != nullptr)
		freeaddrinfo(resolved);

	Diag("=== network probe: outbound part done ===");
	DiagFlushToSd();

	// --- Phase 3: listening socket (we want the Wii U to be able to host) ---
	rc = socket(AF_INET, SOCK_STREAM, 0);
	ProbeStep("socket(listener)", rc);
	const int srv = rc;
	if (srv < 0) {
		DiagFlushToSd();
		return;
	}

	const int one = 1;
	rc = setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	ProbeStep("setsockopt(SO_REUSEADDR)", rc);

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(6112);
	rc = bind(srv, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
	ProbeStep("bind(0.0.0.0:6112)", rc);
	rc = listen(srv, 4);
	ProbeStep("listen(backlog=4)", rc);

	socklen_t addrLen = sizeof(addr);
	rc = getsockname(srv, reinterpret_cast<sockaddr *>(&addr), &addrLen);
	ProbeStep("getsockname", rc);
	Diag("  bound port = %d", ntohs(addr.sin_port));

	rc = socket(AF_INET, SOCK_STREAM, 0);
	ProbeStep("socket(client)", rc);
	const int cli = rc;
	if (cli < 0) {
		close(srv);
		DiagFlushToSd();
		return;
	}

	sockaddr_in dst {};
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	dst.sin_port = addr.sin_port;
	rc = connect(cli, reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
	ProbeStep("connect(127.0.0.1)", rc);

	Diag("=== network probe: before accept ===");
	DiagFlushToSd();

	sockaddr_in peer {};
	socklen_t peerLen = sizeof(peer);
	rc = accept(srv, reinterpret_cast<sockaddr *>(&peer), &peerLen);
	ProbeStep("accept", rc);
	const int acc = rc;
	if (acc >= 0) {
		rc = static_cast<int>(send(cli, "ping", 4, 0));
		ProbeStep("send(\"ping\")", rc);

		Diag("  --- select(2s) ---");
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(acc, &readSet);
		timeval loopbackTimeout { 2, 0 };
		rc = select(acc + 1, &readSet, nullptr, nullptr, &loopbackTimeout);
		ProbeStep("select(acc+1, 2s)", rc);
		if (rc > 0)
			Diag("  FD_ISSET(acc) = %d", FD_ISSET(acc, &readSet) ? 1 : 0);

		// Cemu's internal loopback never delivers data (select always times out),
		// so recv() is probed on the real socket above instead.
		close(acc);
	}
	close(cli);
	close(srv);

	// --- Phase 4: reach the game server (dvlnet listens on 6112) ---
	// Run a native build as host and this tells us, before any UI exists,
	// whether the guest can actually connect to it.
	rc = socket(AF_INET, SOCK_STREAM, 0);
	ProbeStep("socket(server)", rc);
	const int srvCli = rc;
	if (srvCli >= 0) {
		sockaddr_in server {};
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		server.sin_port = htons(6112);
		rc = connect(srvCli, reinterpret_cast<sockaddr *>(&server), sizeof(server));
		ProbeStep("connect(127.0.0.1:6112)", rc);
		if (rc == 0) {
			rc = static_cast<int>(send(srvCli, "dvlnet-probe", 12, 0));
			ProbeStep("send(server)", rc);
		}
		close(srvCli);
	}

	Diag("=== network probe done ===");
	DiagFlushToSd();
}

} // namespace

void wiiu_init()
{
	DiagHeader();
	LogEnvironment();
	SetupPaths();
	wiiu_log_paths();
#ifdef __WIIU__
#ifndef NONET
	wiiu_enable_network();
	Diag("network: wiiu_enable_network() done");
#endif
#ifdef PACKET_ENCRYPTION
	Diag("random: %s", wiiu_random_source());
#endif
	ProbeNetworking();
#ifndef NONET
	wiiu_probe_asio();
#endif
#endif
}

void wiiu_log_paths()
{
	// Called once from wiiu_init() and once from diablo.cpp after ApplicationInit(),
	// so label the second pass instead of repeating the section verbatim.
	static bool loggedOnce = false;
	Diag("=== resolved devilution paths%s ===", loggedOnce ? " (re-checked after ApplicationInit)" : "");
	loggedOnce = true;

	Diag("  BasePath   = '%s'", devilution::paths::BasePath().c_str());
	Diag("  AssetsPath = '%s'", devilution::paths::AssetsPath().c_str());
	Diag("  PrefPath   = '%s'", devilution::paths::PrefPath().c_str());
	Diag("  ConfigPath = '%s'", devilution::paths::ConfigPath().c_str());

	// On real hardware `/vol/save` is the per-title save volume. It does not exist
	// on Cemu, but if it shows up it is a better place for saves than the SD card.
	// Not used yet — this only records whether it is there.
	Diag("  /vol/save exists=%s writable=%s", FileExists("/vol/save") ? "yes" : "no",
	    DirExists("/vol/save") && IsWritableDir("/vol/save") ? "yes" : "no");

	DiagFlushToSd();
}
