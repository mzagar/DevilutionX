#pragma once

#include_next <errno.h>

// asio/error.hpp references ESHUTDOWN unconditionally on POSIX builds.
#ifndef ESHUTDOWN
#define ESHUTDOWN 110 /* can't send after socket shutdown */
#endif
