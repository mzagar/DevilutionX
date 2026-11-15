#include "dvlnet/local_client.h"

#include <memory>

#include "multi.h"
#include "player.h"
#include "utils/log.hpp"

namespace devilution::net {

local_client::~local_client()
{
	if (endpoint_ != MAX_PLRS)
		local_bus::Instance().detach(endpoint_);
}

int local_client::create(std::string_view /*addrstr*/)
{
	return Attach(/*isHost=*/true);
}

int local_client::join(std::string_view /*addrstr*/)
{
	return Attach(/*isHost=*/false);
}

int local_client::Attach(bool isHost)
{
	if (endpoint_ != MAX_PLRS)
		return -1;

	// The packet factory is created by the password hooks on every provider.
	clear_password();

	endpoint_ = local_bus::Instance().attach();
	if (endpoint_ >= MAX_PLRS) {
		SDL_SetError("Local session is full");
		return -1;
	}

	plr_self = endpoint_;
	isGameHost_ = isHost;
	if (std::expected<void, PacketError> result = Connect(plr_self); !result.has_value()) {
		SDL_SetError("%s", std::string(result.error().what()).c_str());
		local_bus::Instance().detach(endpoint_);
		endpoint_ = MAX_PLRS;
		plr_self = PLR_BROADCAST;
		return -1;
	}
	ConnectAttachedPeers();
	return plr_self;
}

void local_client::ConnectAttachedPeers()
{
	// Turns are gated on every connected player, so the roster must be complete
	// before the first tick. RecvLocal() would do this lazily on the first packet.
	for (plr_t player = 0; player < Players.size() && player < MAX_PLRS; player++) {
		if (player == plr_self || !local_bus::Instance().is_attached(player))
			continue;
		if (std::expected<void, PacketError> result = Connect(player); !result.has_value())
			LogError("local connect {}: {}", player, result.error().what());
	}
}

std::expected<void, PacketError> local_client::poll()
{
	local_bus &bus = local_bus::Instance();
	if (!bus.is_attached(endpoint_))
		return {};

	local_bus::endpoint from = MAX_PLRS;
	buffer_t payload;
	while (bus.recv(endpoint_, from, payload)) {
		std::expected<std::unique_ptr<packet>, PacketError> pkt = pktfty->make_packet(payload);
		if (!pkt.has_value())
			return std::unexpected(pkt.error());
		std::expected<void, PacketError> result = RecvLocal(**pkt);
		if (!result.has_value())
			return result;
	}

	for (const local_bus::endpoint gone : bus.take_disconnected(endpoint_))
		DisconnectNet(gone);

	return {};
}

std::expected<void, PacketError> local_client::send(packet &pkt)
{
	const plr_t destination = pkt.Destination();
	if (destination == PLR_BROADCAST) {
		local_bus::Instance().broadcast(endpoint_, pkt.Data());
		return {};
	}
	if (destination >= MAX_PLRS)
		return std::unexpected("Invalid player ID");
	if (destination == plr_self)
		return {};
	if (!local_bus::Instance().send(endpoint_, destination, pkt.Data()))
		return std::unexpected("Local peer is not attached");
	return {};
}

void local_client::DisconnectNet(plr_t plr)
{
	local_bus::Instance().detach(plr);
}

std::string local_client::make_default_gamename()
{
	return "local";
}

bool local_client::IsGameHost()
{
	return isGameHost_;
}

} // namespace devilution::net