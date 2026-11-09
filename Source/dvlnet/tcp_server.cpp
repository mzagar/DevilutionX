#include "dvlnet/tcp_server.h"

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <utility>

#include "dvlnet/base.h"
#include "player.h"
#include "utils/log.hpp"

namespace devilution::net {

tcp_server::tcp_server(asio::io_context &ioc, const std::string &bindaddr,
    unsigned short port, packet_factory &pktfty)
    : ioc(ioc)
    , pktfty(pktfty)
{
	boundAddress_ = asio::ip::make_address(bindaddr);
	boundPort_ = port;
	const asio::ip::tcp::endpoint ep(boundAddress_, port);

	// Explicit error handling instead of the throwing constructor: a console
	// socket stack may reject individual options (reuse_address is optional),
	// and asio's exception hook is fatal here.
	acceptor = std::make_unique<asio::ip::tcp::acceptor>(ioc);
	asio::error_code errorCode;
	acceptor->open(ep.protocol(), errorCode);
	if (errorCode)
		LogError("tcp_server: open failed: {}", errorCode.message());
	acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true), errorCode);
	if (errorCode) {
		LogError("tcp_server: reuse_address failed: {}", errorCode.message());
		errorCode.clear();
	}
	acceptor->bind(ep, errorCode);
	if (errorCode)
		LogError("tcp_server: bind {}:{} failed: {}", boundAddress_.to_string(), port, errorCode.message());
	acceptor->listen(asio::socket_base::max_listen_connections, errorCode);
	if (errorCode)
		LogError("tcp_server: listen failed: {}", errorCode.message());
	StartAccept();
}

std::string tcp_server::LocalhostSelf()
{
	// The host connects to its own listener through `tcp_client::join()`,
	// which parses "host:port". Return the port as well: the bound port is
	// not necessarily the default 6112 (e.g. when Network.port is set),
	// and without it the self-connect went to the wrong port.
	asio::ip::address addr = boundAddress_;
	unsigned short port = boundPort_;

	// Cemu's nsysnet reports getsockname() as successful but returns an
	// out-of-range sockaddr length, which makes asio's endpoint::resize()
	// throw (fatally in this build) even from the error_code overload. The
	// requested endpoint is what we need anyway, so only query the socket for
	// the port when it was left unspecified (port 0 = ephemeral).
	if (boundPort_ == 0) {
		asio::error_code errorCode;
		const auto localEndpoint = acceptor->local_endpoint(errorCode);
		if (!errorCode) {
			addr = localEndpoint.address();
			port = localEndpoint.port();
		}
	}

	if (addr.is_unspecified()) {
		if (addr.is_v6()) {
			addr = asio::ip::address_v6::loopback();
		} else {
			addr = asio::ip::address_v4::loopback();
		}
	}
	if (addr.is_v6()) {
		return "[" + addr.to_string() + "]:" + std::to_string(port);
	}
	return addr.to_string() + ":" + std::to_string(port);
}

tcp_server::scc tcp_server::MakeConnection()
{
	return std::make_shared<client_connection>(ioc);
}

plr_t tcp_server::NextFree()
{
	for (plr_t i = 0; i < Players.size(); ++i)
		if (!connections[i])
			return i;
	return PLR_BROADCAST;
}

bool tcp_server::Empty()
{
	for (plr_t i = 0; i < Players.size(); ++i)
		if (connections[i])
			return false;
	return true;
}

void tcp_server::StartReceive(const scc &con)
{
	con->socket.async_receive(
	    asio::buffer(con->recv_buffer),
	    std::bind(&tcp_server::HandleReceive, this, con, std::placeholders::_1, std::placeholders::_2));
}

void tcp_server::HandleReceive(const scc &con, const asio::error_code &ec,
    size_t bytesRead)
{
	if (ec || bytesRead == 0) {
		DropConnection(con);
		return;
	}
	con->recv_buffer.resize(bytesRead);
	con->recv_queue.Write(std::move(con->recv_buffer));
	con->recv_buffer.resize(frame_queue::max_frame_size);
	while (true) {
		std::expected<bool, PacketError> ready = con->recv_queue.PacketReady();
		if (!ready.has_value()) {
			Log("PacketReady: {}", ready.error().what());
			DropConnection(con);
			return;
		}
		if (!*ready)
			break;
		std::expected<buffer_t, PacketError> pktData = con->recv_queue.ReadPacket();
		if (!pktData.has_value()) {
			Log("ReadPacket: {}", pktData.error().what());
			DropConnection(con);
			return;
		}
		std::expected<std::unique_ptr<packet>, PacketError> pkt = pktfty.make_packet(*pktData);
		if (!pkt.has_value()) {
			Log("make_packet: {}", pkt.error().what());
			if (pkt.error().code() == PacketError::ErrorCode::DecryptionFailed)
				StartSend(con, pkt.error().code());
			DropConnection(con);
			return;
		}
		if (con->plr == PLR_BROADCAST) {
			std::expected<void, PacketError> result = HandleReceiveNewPlayer(con, **pkt);
			if (!result.has_value()) {
				Log("HandleReceiveNewPlayer: {}", result.error().what());
				DropConnection(con);
				return;
			}
		} else {
			con->timeout = timeout_active;
			std::expected<void, PacketError> result = HandleReceivePacket(**pkt);
			if (!result.has_value()) {
				Log("Network error: {}", result.error().what());
				DropConnection(con);
				return;
			}
		}
	}
	StartReceive(con);
}

std::expected<void, PacketError> tcp_server::HandleReceiveNewPlayer(const scc &con, packet &inPkt)
{
	auto newplr = NextFree();
	if (newplr == PLR_BROADCAST)
		return std::unexpected(ServerError());

	if (Empty()) {
		std::expected<const buffer_t *, PacketError> pktInfo = inPkt.Info();
		if (!pktInfo.has_value())
			return std::unexpected(pktInfo.error());
		game_init_info = **pktInfo;
	}

	for (plr_t player = 0; player < Players.size(); player++) {
		if (connections[player]) {
			std::expected<void, PacketError> result
			    = pktfty.make_packet<PT_CONNECT>(PLR_MASTER, PLR_BROADCAST, newplr)
			          .and_then([&](std::unique_ptr<packet> &&pkt) { return StartSend(connections[player], *pkt); })
			          .and_then([&]() { return pktfty.make_packet<PT_CONNECT>(PLR_MASTER, PLR_BROADCAST, player); })
			          .and_then([&](std::unique_ptr<packet> &&pkt) { return StartSend(con, *pkt); });
			if (!result.has_value())
				return result;
		}
	}

	std::expected<void, PacketError> result
	    = inPkt.Cookie()
	          .and_then([&](cookie_t &&cookie) { return pktfty.make_packet<PT_JOIN_ACCEPT>(PLR_MASTER, PLR_BROADCAST, cookie, newplr, game_init_info); })
	          .and_then([&](std::unique_ptr<packet> &&pkt) { return StartSend(con, *pkt); });
	if (!result.has_value())
		return result;
	con->plr = newplr;
	connections[newplr] = con;
	con->timeout = timeout_active;
	return {};
}

std::expected<void, PacketError> tcp_server::HandleReceivePacket(packet &pkt)
{
	return SendPacket(pkt);
}

std::expected<void, PacketError> tcp_server::SendPacket(packet &pkt)
{
	if (pkt.Destination() == PLR_BROADCAST) {
		for (size_t i = 0; i < Players.size(); ++i) {
			if (i == pkt.Source() || !connections[i])
				continue;
			std::expected<void, PacketError> result = StartSend(connections[i], pkt);
			if (!result.has_value())
				LogError("Failed to send packet {} to player {}: {}", static_cast<uint8_t>(pkt.Type()), i, result.error().what());
		}
		return {};
	}
	if (pkt.Destination() >= MAX_PLRS)
		return std::unexpected(ServerError());
	if (pkt.Destination() == pkt.Source() || !connections[pkt.Destination()])
		return {};
	return StartSend(connections[pkt.Destination()], pkt);
}

std::expected<void, PacketError> tcp_server::StartSend(const scc &con, packet &pkt)
{
	return StartSend(con, pkt.Data(), 0);
}

std::expected<void, PacketError> tcp_server::StartSend(const scc &con, PacketError::ErrorCode errorCode)
{
	buffer_t pktData;
	pktData.push_back(static_cast<unsigned char>(errorCode));
	return StartSend(con, pktData, TcpErrorCodeFlags);
}

std::expected<void, PacketError> tcp_server::StartSend(const scc &con, buffer_t pktData, uint16_t flags)
{
	std::expected<buffer_t, PacketError> frame = frame_queue::MakeFrame(pktData, flags);
	if (!frame.has_value())
		return std::unexpected(frame.error());
	con->sendQueue.push_back(std::move(*frame));
	if (!con->sending)
		SendNext(con);
	return {};
}

void tcp_server::SendNext(const scc &con)
{
	if (con->sendQueue.empty()) {
		con->sending = false;
		return;
	}
	con->sending = true;
	// The write handler owns this buffer, so the queue can keep receiving
	// while the previous frame is on the wire.
	auto framePtr = std::make_unique<buffer_t>(std::move(con->sendQueue.front()));
	con->sendQueue.pop_front();
	asio::async_write(con->socket, asio::buffer(*framePtr),
	    [this, con, frame = std::move(framePtr)](const asio::error_code &ec, size_t bytesSent) {
		    HandleSend(con, ec, bytesSent);
	    });
}

void tcp_server::HandleSend(const scc &con, const asio::error_code &ec,
    size_t /*bytesSent*/)
{
	if (ec) {
		Log("Network error: {}", ec.message());
		con->sending = false;
		con->sendQueue.clear();
		DropConnection(con);
		return;
	}
	SendNext(con);
}

void tcp_server::StartAccept()
{
	auto nextcon = MakeConnection();
	acceptor->async_accept(
	    nextcon->socket,
	    std::bind(&tcp_server::HandleAccept, this, nextcon, std::placeholders::_1));
}

void tcp_server::HandleAccept(const scc &con, const asio::error_code &ec)
{
	if (ec) {
		const PacketError packetError = IoHandlerError(ec.message());
		RaiseIoHandlerError(packetError);
		return;
	}
	if (NextFree() == PLR_BROADCAST) {
		DropConnection(con);
	} else {
		asio::error_code errorCode;
		const asio::ip::tcp::no_delay option(true);
		con->socket.set_option(option, errorCode);
		if (errorCode)
			LogError("Server error setting socket option: {}", errorCode.message());
		con->timeout = timeout_connect;
		StartReceive(con);
		StartTimeout(con);
	}
	StartAccept();
}

void tcp_server::StartTimeout(const scc &con)
{
	con->timer.expires_after(std::chrono::seconds(1));
	con->timer.async_wait(
	    std::bind(&tcp_server::HandleTimeout, this, con, std::placeholders::_1));
}

void tcp_server::HandleTimeout(const scc &con, const asio::error_code &ec)
{
	if (ec) {
		DropConnection(con);
		return;
	}
	if (con->timeout > 0)
		con->timeout -= 1;
	if (con->timeout <= 0) {
		con->timeout = 0;
		DropConnection(con);
		return;
	}
	StartTimeout(con);
}

void tcp_server::DropConnection(const scc &con)
{
	const plr_t plr = con->plr;
	con->timer.cancel();
	con->socket.close();
	if (plr == PLR_BROADCAST) {
		return;
	}
	connections[plr] = nullptr;

	std::expected<std::unique_ptr<packet>, PacketError> pkt
	    = pktfty.make_packet<PT_DISCONNECT>(PLR_MASTER, PLR_BROADCAST,
	        plr, leaveinfo_t::LEAVE_DROP);
	if (pkt.has_value()) {
		SendPacket(**pkt);
	} else {
		LogError("make_packet<PT_DISCONNECT>: {}", pkt.error().what());
	}
}

void tcp_server::RaiseIoHandlerError(const PacketError &error)
{
	ioHandlerResult.emplace(error);
}

std::expected<void, PacketError> tcp_server::CheckIoHandlerError()
{
	if (ioHandlerResult == std::nullopt)
		return {};
	std::expected<void, PacketError> packetError = std::unexpected(*ioHandlerResult);
	ioHandlerResult = std::nullopt;
	return packetError;
}

void tcp_server::DisconnectNet(plr_t plr)
{
	scc &con = connections[plr];
	if (con == nullptr)
		return;
	con->timer.cancel();
	con->socket.close();
	con = nullptr;
}

void tcp_server::Close()
{
	acceptor->close();
}

tcp_server::~tcp_server()
    = default;

} // namespace devilution::net
