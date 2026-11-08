#pragma once

// wut's <netinet/in.h> is IPv4-only; asio expects the IPv6 types and constants.

#include <stdint.h>

#include_next <netinet/in.h>

struct in6_addr {
	uint8_t s6_addr[16];
};

struct ipv6_mreq {
	struct in6_addr ipv6mr_multiaddr;
	unsigned ipv6mr_interface;
};

struct sockaddr_in6 {
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

#define IPPROTO_IPV6 -1
#define IPV6_JOIN_GROUP -1
#define IPV6_LEAVE_GROUP -1
#define IPV6_UNICAST_HOPS -1
#define IPV6_MULTICAST_HOPS -1
#define IPV6_MULTICAST_IF -1
#define IPV6_MULTICAST_LOOP -1
#define IPV6_V6ONLY -1
