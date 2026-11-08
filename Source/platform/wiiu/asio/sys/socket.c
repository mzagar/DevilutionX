#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

// wut/nsysnet has no recvmsg/sendmsg/socketpair. asio only needs the
// stream case, so scatter/gather is folded into plain recv/send calls.
// Modelled on Source/platform/ctr/asio/sys/socket.c.

static ssize_t stream_recvmsg(int socket, struct msghdr *message, int flags)
{
	ssize_t total = 0;
	struct iovec *next = message->msg_iov;
	const int iovcount = message->msg_iovlen;

	for (int i = 0; i < iovcount; ++i, ++next) {
		void *base = next->iov_base;
		size_t length = next->iov_len;

		while (length > 0) {
			const ssize_t bytesReceived = recv(socket, base, length, flags);
			if (bytesReceived == -1) {
				if (total > 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
					return total;
				return -1;
			}
			base = (char *)base + bytesReceived;
			length -= (size_t)bytesReceived;
			total += bytesReceived;

			if ((flags & MSG_WAITALL) == 0)
				return total;
		}
	}
	return total;
}

static ssize_t stream_sendmsg(int socket, const struct msghdr *message, int flags)
{
	ssize_t total = 0;
	struct iovec *next = message->msg_iov;
	const int iovcount = message->msg_iovlen;

	for (int i = 0; i < iovcount; ++i, ++next) {
		if (next->iov_len == 0)
			continue;

		const ssize_t bytesSent = send(socket, next->iov_base, next->iov_len, flags);
		if (bytesSent == -1) {
			if (total > 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				return total;
			return -1;
		}
		total += bytesSent;
	}
	return total;
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags)
{
	int type;
	socklen_t length = sizeof(int);
	if (getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &length) == -1)
		return -1;

	if (type == SOCK_STREAM)
		return stream_recvmsg(socket, message, flags);

	errno = ENOTSOCK;
	return -1;
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags)
{
	int type;
	socklen_t length = sizeof(int);
	if (getsockopt(socket, SOL_SOCKET, SO_TYPE, &type, &length) == -1)
		return -1;

	if (type == SOCK_STREAM)
		return stream_sendmsg(socket, message, flags);

	errno = ENOTSOCK;
	return -1;
}

int socketpair(int domain, int type, int protocol, int socket_vector[2])
{
	return ENOTSUP;
}
