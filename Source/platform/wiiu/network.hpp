#pragma once

/// Initialises the Wii U socket library (nsysnet). Safe to call twice: the
/// library is reference-counted and wiiu_disable_network() is registered with
/// atexit.
void wiiu_enable_network();
void wiiu_disable_network();

/// TEMPORARY: OSReport + boot-diag line used by the network bring-up probe.
void wiiu_log_net(const char *fmt, ...);

#ifndef NONET
/// TEMPORARY: proves asio itself works on Wii U. Remove once dvlnet is verified.
void wiiu_probe_asio();
#endif
