#include <errno.h>

// asio's null_event::do_wait() calls pause() on POSIX. wut has no pause().
int pause(void)
{
	errno = ENOSYS;
	return -1;
}
