#pragma once

// wut's <sys/socket.h> has no struct msghdr and no scatter/gather entry points;
// asio includes <sys/socket.h> and expects them. The implementations live in
// asio/sys/socket.c (stream-only, built on recv/send).

#include_next <sys/socket.h>

#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct msghdr {
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
	int msg_iovlen;
	void *msg_control;
	socklen_t msg_controllen;
	int msg_flags;
};

#ifndef MSG_WAITALL
#define MSG_WAITALL 0x0040
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x0000
#endif
#ifndef MSG_EOR
#define MSG_EOR 0x0080
#endif
#ifndef SOMAXCONN
#define SOMAXCONN 8
#endif
// nsysnet has no RAW/SEQPACKET support; asio's socket_types.hpp references these
// unconditionally on POSIX builds.
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif
#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6 58
#endif
// Present on other platforms; nsysnet has no equivalent. Values are inert.
#ifndef SO_DEBUG
#define SO_DEBUG 0x0001
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif

ssize_t recvmsg(int socket, struct msghdr *message, int flags);
ssize_t sendmsg(int socket, const struct msghdr *message, int flags);
int socketpair(int domain, int type, int protocol, int socket_vector[2]);

#ifdef __cplusplus
}
#endif
