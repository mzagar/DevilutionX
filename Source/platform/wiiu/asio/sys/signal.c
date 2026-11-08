#include <errno.h>
#include <sys/signal.h>

// asio's posix_signal_blocker blocks signals around scheduler setup. wut has
// no signal support; the calls are harmless no-ops here.
int pthread_sigmask(int, const sigset_t *, sigset_t *)
{
	return ENOTSUP;
}
