#pragma once

// wut has no <sys/uio.h> and no <sys/_iovec.h>; asio needs iovec/readv/writev.
// readv/writev themselves are stubbed out in asio/sys/uio.c.

#include <sys/types.h>

struct iovec {
	void *iov_base;
	size_t iov_len;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int __fd, const struct iovec *__iovec, int __count);
ssize_t writev(int __fd, const struct iovec *__iovec, int __count);

#ifdef __cplusplus
}
#endif
