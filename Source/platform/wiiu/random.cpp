/**
 * @file random.cpp
 * @brief libsodium `randombytes` backend for the Wii U.
 *
 * libsodium's default `sysrandom` implementation opens `/dev/urandom` and calls
 * `sodium_misuse()` (-> OSFatal on this port) when that fails. WUT/CafeOS
 * exposes no random device and no `getentropy`/`arc4random` to userland, so
 * the default backend aborts on the first cookie generation - that is, on the
 * very first TCP join, before any UI.
 *
 * This file installs a platform implementation instead:
 *
 * 1. If a random device can be opened it is used directly (preferred; the
 *    port probes `/dev/urandom` on every supported environment).
 * 2. Otherwise a ChaCha20-based DRBG is used. It is seeded with everything the
 *    console exposes: the OS clock, the free-running timer, `OSGetOSID()`, the
 *    system clock speeds, thread/stack addresses, uninitialised stack bytes and
 *    timer jitter sampled in a loop. The DRBG rekeys from fresh entropy every
 *    64 KiB.
 *
 * Caveat, recorded deliberately: without a hardware RNG the seed has limited
 * entropy, so this is a best-effort CSPRNG, not an audited one. It is strictly
 * better than aborting, and it only protects the optional password on a LAN
 * game; `boot-diag.txt` and the harness report which path was taken so the
 * question can be re-opened on real hardware.
 */
#include "platform/wiiu/random.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <coreinit/systeminfo.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>

#include <fcntl.h>
#include <unistd.h>

#include <sodium.h>

namespace {

// ---------------------------------------------------------------------------
// Minimal ChaCha20 core (RFC 8439), used for both key derivation and output.
// Deliberately independent of libsodium: this runs during `sodium_init()`,
// before libsodium's CPU feature detection has happened.
// ---------------------------------------------------------------------------

inline uint32_t Rotl32(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

inline void QuarterRound(uint32_t &a, uint32_t &b, uint32_t &c, uint32_t &d)
{
	a += b;
	d ^= a;
	d = Rotl32(d, 16);
	c += d;
	b ^= c;
	b = Rotl32(b, 12);
	a += b;
	d ^= a;
	d = Rotl32(d, 8);
	c += d;
	b ^= c;
	b = Rotl32(b, 7);
}

void ChaCha20Block(const uint32_t key[8], uint32_t counter, const uint32_t nonce[3], unsigned char out[64])
{
	uint32_t state[16] = {
		0x61707865, 0x3320646E, 0x79622D32, 0x6B206574,
		key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
		counter, nonce[0], nonce[1], nonce[2]
	};
	uint32_t working[16];
	std::memcpy(working, state, sizeof(working));

	for (int round = 0; round < 10; round++) {
		QuarterRound(working[0], working[4], working[8], working[12]);
		QuarterRound(working[1], working[5], working[9], working[13]);
		QuarterRound(working[2], working[6], working[10], working[14]);
		QuarterRound(working[3], working[7], working[11], working[15]);
		QuarterRound(working[0], working[5], working[10], working[15]);
		QuarterRound(working[1], working[6], working[11], working[12]);
		QuarterRound(working[2], working[7], working[8], working[13]);
		QuarterRound(working[3], working[4], working[9], working[14]);
	}

	for (int i = 0; i < 16; i++) {
		const uint32_t value = working[i] + state[i];
		out[i * 4 + 0] = static_cast<unsigned char>(value);
		out[i * 4 + 1] = static_cast<unsigned char>(value >> 8);
		out[i * 4 + 2] = static_cast<unsigned char>(value >> 16);
		out[i * 4 + 3] = static_cast<unsigned char>(value >> 24);
	}
}

void Scrub(void *data, size_t size)
{
	volatile unsigned char *bytes = static_cast<volatile unsigned char *>(data);
	while (size-- > 0)
		*bytes++ = 0;
}

// ---------------------------------------------------------------------------
// DRBG state
// ---------------------------------------------------------------------------

struct DrbgState {
	uint32_t key[8];
	uint64_t blockCounter;
	bool seeded;
};

DrbgState g_drbg;

const char *g_source = "uninitialised";
int g_randomDevice = -1;

/// Collects 64 bytes of whatever entropy the console provides.
void GatherEntropy(unsigned char entropy[64])
{
	std::memset(entropy, 0, 64);

	uint64_t values[5];
	values[0] = static_cast<uint64_t>(OSGetSystemTime());
	values[1] = static_cast<uint64_t>(OSGetTick());
	values[2] = OSGetOSID();
	const OSSystemInfo *systemInfo = OSGetSystemInfo();
	values[3] = systemInfo != nullptr
	    ? (static_cast<uint64_t>(systemInfo->busClockSpeed) << 32) | systemInfo->coreClockSpeed
	    : 0;
	values[4] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&values));
	std::memcpy(entropy, values, sizeof(values));

	// Timer jitter: the low bits of the free-running tick counter are driven by
	// interrupt timing, which is not reproducible across boots or siblings.
	for (int i = 0; i < 16; i++) {
		const uint32_t tick = static_cast<uint32_t>(OSGetTick());
		entropy[40 + (i % 8) * 2] ^= static_cast<unsigned char>(tick);
		entropy[41 + (i % 8) * 2] ^= static_cast<unsigned char>(tick >> 8);
		entropy[56 + (i % 4)] ^= static_cast<unsigned char>(tick >> 16);
		entropy[60 + (i % 2)] ^= static_cast<unsigned char>(tick >> 24);
	}

	// Wall clock + per-process/thread identity.
	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	std::memcpy(entropy + 16, &now, sizeof(now));
	const uintptr_t threadId = reinterpret_cast<uintptr_t>(OSGetCurrentThread());
	std::memcpy(entropy + 24, &threadId, sizeof(threadId));
	const uintptr_t stackAddress = reinterpret_cast<uintptr_t>(&now);
	std::memcpy(entropy + 32, &stackAddress, sizeof(stackAddress));

	// Uninitialised stack bytes. Indeterminate by the letter of the standard,
	// but a standard entropy source on platforms without a hardware RNG.
	volatile unsigned char stackNoise[32];
	for (size_t i = 0; i < sizeof(stackNoise); i++)
		entropy[16 + i] ^= stackNoise[i];
}

void DeriveKey(const unsigned char entropy[64])
{
	unsigned char block0[64];
	unsigned char block1[64];
	uint32_t nonce[3] = {
		static_cast<uint32_t>(entropy[40]) | (static_cast<uint32_t>(entropy[41]) << 8) | (static_cast<uint32_t>(entropy[42]) << 16) | (static_cast<uint32_t>(entropy[43]) << 24),
		static_cast<uint32_t>(entropy[44]) | (static_cast<uint32_t>(entropy[45]) << 8) | (static_cast<uint32_t>(entropy[46]) << 16) | (static_cast<uint32_t>(entropy[47]) << 24),
		static_cast<uint32_t>(entropy[48]) | (static_cast<uint32_t>(entropy[49]) << 8) | (static_cast<uint32_t>(entropy[50]) << 16) | (static_cast<uint32_t>(entropy[51]) << 24),
	};

	if (g_drbg.seeded) {
		// key' = PRF(key, entropy)
		ChaCha20Block(g_drbg.key, 0, nonce, block0);
		ChaCha20Block(g_drbg.key, 1, nonce, block1);
	} else {
		// First seed: the entropy *is* the key material.
		uint32_t seedKey[8];
		std::memcpy(seedKey, entropy, 32);
		ChaCha20Block(seedKey, 0, nonce, block0);
		ChaCha20Block(seedKey, 1, nonce, block1);
		Scrub(seedKey, sizeof(seedKey));
	}

	for (size_t i = 0; i < 32; i++)
		block0[i] ^= block1[i] ^ entropy[i];
	std::memcpy(g_drbg.key, block0, 32);
	g_drbg.blockCounter = 0;
	g_drbg.seeded = true;

	Scrub(block0, sizeof(block0));
	Scrub(block1, sizeof(block1));
}

void Reseed()
{
	// Reached only when no random device is available.
	g_source = "chacha20-drbg";
	unsigned char entropy[64];
	GatherEntropy(entropy);
	DeriveKey(entropy);
	Scrub(entropy, sizeof(entropy));
}

bool TryRandomDevice()
{
	if (g_randomDevice >= 0)
		return true;
	g_randomDevice = open("/dev/urandom", O_RDONLY);
	if (g_randomDevice < 0)
		return false;
	g_source = "/dev/urandom";
	return true;
}

bool ReadRandomDevice(void *buffer, size_t size)
{
	auto *bytes = static_cast<unsigned char *>(buffer);
	while (size > 0) {
		const ssize_t readBytes = read(g_randomDevice, bytes, size);
		if (readBytes <= 0) {
			close(g_randomDevice);
			g_randomDevice = -1;
			g_source = "chacha20-drbg";
			return false;
		}
		bytes += readBytes;
		size -= static_cast<size_t>(readBytes);
	}
	return true;
}

void DrbgGenerate(void *buffer, size_t size)
{
	if (!g_drbg.seeded)
		Reseed();

	auto *bytes = static_cast<unsigned char *>(buffer);
	while (size > 0) {
		unsigned char block[64];
		const uint32_t nonce[3] = { 0, 0, 0 };
		ChaCha20Block(g_drbg.key, static_cast<uint32_t>(g_drbg.blockCounter), nonce, block);
		g_drbg.blockCounter++;

		const size_t chunk = size < sizeof(block) ? size : sizeof(block);
		std::memcpy(bytes, block, chunk);
		Scrub(block, sizeof(block));

		bytes += chunk;
		size -= chunk;

		// Rekey from fresh entropy after 64 KiB.
		if (g_drbg.blockCounter % 1024 == 0)
			Reseed();
	}
}

void Generate(void *buffer, size_t size)
{
	if (TryRandomDevice() && ReadRandomDevice(buffer, size))
		return;
	DrbgGenerate(buffer, size);
}

// ---------------------------------------------------------------------------
// libsodium glue
// ---------------------------------------------------------------------------

const char *ImplementationName()
{
	return "wiiurandom";
}

uint32_t ImplementationRandom()
{
	uint32_t value;
	Generate(&value, sizeof(value));
	return value;
}

void ImplementationStir()
{
	if (TryRandomDevice())
		return;
	Reseed();
}

void ImplementationBuffer(void *buffer, size_t size)
{
	Generate(buffer, size);
}

int ImplementationClose()
{
	if (g_randomDevice >= 0) {
		close(g_randomDevice);
		g_randomDevice = -1;
	}
	std::memset(&g_drbg, 0, sizeof(g_drbg));
	return 0;
}

randombytes_implementation g_implementation = {
	ImplementationName,
	ImplementationRandom,
	ImplementationStir,
	nullptr,
	ImplementationBuffer,
	ImplementationClose,
};

} // namespace

void randombytes_wiiurandom_init()
{
	// Install before sodium_init() (which calls randombytes_stir()) so the
	// default `sysrandom` implementation never runs and never aborts.
	randombytes_set_implementation(&g_implementation);
	ImplementationStir();
}

const char *wiiu_random_source()
{
	return g_source;
}
