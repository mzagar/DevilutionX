#pragma once

/// Installs a libsodium `randombytes` implementation for the Wii U.
///
/// Must be called before the first libsodium RNG use (i.e. before
/// `SNetInitializeProvider`, which creates the packet factory). See random.cpp
/// for why this port cannot use libsodium's default `sysrandom` backend.
void randombytes_wiiurandom_init();

/// Human-readable description of the entropy source in use, for logs and
/// `boot-diag.txt`. Valid after randombytes_wiiurandom_init().
const char *wiiu_random_source();
