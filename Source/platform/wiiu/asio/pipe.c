#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// wut has no pipe(). asio's select reactor only uses it as a self-wakeup
// mechanism: it select()s on fildes[0] and write()s a byte to fildes[1] to
// interrupt a blocking wait. A connected loopback TCP pair is a faithful
// stand-in -- same byte-stream semantics, same readiness behaviour.
//
// (Cemu's emulated loopback accepts a connection and reports success for
// write(), but never makes the peer readable, so in Cemu the reactor simply
// never gets interrupted early; on real hardware it behaves as a pipe.)
int pipe(int fildes[2])
{
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0)
		return -1;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(listener);
		return -1;
	}

	socklen_t addrLen = sizeof(addr);
	if (getsockname(listener, (struct sockaddr *)&addr, &addrLen) != 0) {
		close(listener);
		return -1;
	}

	if (listen(listener, 1) != 0) {
		close(listener);
		return -1;
	}

	const int writer = socket(AF_INET, SOCK_STREAM, 0);
	if (writer < 0) {
		close(listener);
		return -1;
	}

	if (connect(writer, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(writer);
		close(listener);
		return -1;
	}

	struct sockaddr_in peer;
	socklen_t peerLen = sizeof(peer);
	const int reader = accept(listener, (struct sockaddr *)&peer, &peerLen);
	close(listener);

	if (reader < 0) {
		close(writer);
		return -1;
	}

	fildes[0] = reader;
	fildes[1] = writer;
	return 0;
}
