#pragma once

// wut ships <poll.h> but not the POSIX-compatible <sys/poll.h> that asio includes.
#include <poll.h>
