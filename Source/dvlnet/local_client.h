#pragma once

#include <string>
#include <string_view>

#include "dvlnet/base.h"
#include "dvlnet/local_bus.h"

namespace devilution::net {

/**
 * One local player's endpoint on the in-process bus.
 *
 * It reuses the production packet codec, message queue and turn tables from
 * net::base; only the transport is replaced. Connection negotiation, discovery
 * and encryption are intentionally absent: the local session coordinator owns
 * the roster and knows all seats up front.
 */
class local_client : public base {
public:
	local_client() = default;
	~local_client() override;

	local_client(const local_client &) = delete;
	local_client &operator=(const local_client &) = delete;

	int create(std::string_view addrstr) override;
	int join(std::string_view addrstr) override;

	std::expected<void, PacketError> poll() override;
	std::expected<void, PacketError> send(packet &pkt) override;

	void DisconnectNet(plr_t plr) override;

	std::string make_default_gamename() override;

protected:
	bool IsGameHost() override;

private:
	int Attach(bool isHost);
	void ConnectAttachedPeers();

	local_bus::endpoint endpoint_ = MAX_PLRS;
	bool isGameHost_ = false;
};

} // namespace devilution::net