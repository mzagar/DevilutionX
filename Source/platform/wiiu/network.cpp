#include "platform/wiiu/network.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <nsysnet/_socket.h>

#ifdef __WIIU__
#include <coreinit/debug.h>
#endif

#ifndef NONET
// Deliberately not <asio.hpp>: the umbrella header pulls in asio::local (Unix
// sockets), which conflicts with wut's flexible-array `struct sockaddr`.
// asio_handle_exception.hpp provides the throw_exception definition that
// ASIO_NO_EXCEPTIONS requires; dvlnet gets it via tcp_client.h/tcp_server.h.
#include <asio_handle_exception.hpp>

#include <asio/buffer.hpp>
#include <asio/connect.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read.hpp>
#include <asio/streambuf.hpp>
#include <asio/write.hpp>
#endif

void wiiu_log_net(const char *fmt, ...)
{
	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
#ifdef __WIIU__
	OSReport("WIIUNET: %s\n", buffer);
#endif
}

void wiiu_enable_network()
{
	socket_lib_init();
	atexit(wiiu_disable_network);
}

void wiiu_disable_network()
{
	socket_lib_finish();
}

#ifndef NONET
// TEMPORARY M-probe: exercises exactly what the dvlnet TCP provider needs
// (asio + nsysnet), without going through the game UI.
void wiiu_probe_asio()
{
	using namespace asio::ip;

	wiiu_log_net("=== asio probe ===");
	try {
		asio::io_context ioContext;
		tcp::resolver resolver(ioContext);

		const auto endpoints = resolver.resolve("example.com", "80");
		if (endpoints.empty()) {
			wiiu_log_net("  resolve returned nothing");
			return;
		}
		wiiu_log_net("  resolved example.com");

		tcp::socket socket(ioContext);
		asio::connect(socket, endpoints);
		wiiu_log_net("  asio::connect ok");

		const std::string request = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
		asio::write(socket, asio::buffer(request));
		wiiu_log_net("  write ok");

		asio::streambuf response;
		std::size_t total = 0;
		// Read whatever arrives first, then stop.
		for (int i = 0; i < 3; ++i) {
			std::error_code ec;
			const std::size_t n = asio::read(socket, response, asio::transfer_at_least(1), ec);
			total += n;
			if (ec)
				break;
		}
		wiiu_log_net("  read %d byte(s) -> asio works", static_cast<int>(total));

		std::error_code ignored;
		socket.close(ignored);
	} catch (const std::exception &e) {
		wiiu_log_net("  EXCEPTION: %s", e.what());
	}
	wiiu_log_net("=== asio probe done ===");
}
#endif
