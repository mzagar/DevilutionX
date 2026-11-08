#pragma once

// wut has no <sys/un.h> (no Unix domain sockets); asio includes it unconditionally.
// sa_family_t is typedef'd by wut's <sys/socket.h>, which asio includes first.

#include <sys/socket.h>
#include <sys/types.h>

struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};
