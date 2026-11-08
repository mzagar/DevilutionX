# wut's pthread thread-specific-storage is a newlib stub: pthread_key_create()
# always returns ENOSYS, which makes asio's posix_tss_ptr throw during static
# initialisation. The 3DS port solves this the same way -- run asio without its
# thread-safety machinery. Each io_context is still usable, just not shared.
target_compile_definitions(asio INTERFACE
  ASIO_DISABLE_THREADS=ON
  ASIO_DISABLE_SERIAL_PORT=ON
  ASIO_HAS_UNISTD_H=ON
  _DEFAULT_SOURCE=ON)

# Missing headers and declarations provided by DevilutionX.
# wut has no <sys/poll.h>, <sys/uio.h>, <sys/un.h>, <net/if.h>, no struct
# msghdr and no IPv6 types; see Source/platform/wiiu/asio/.
target_include_directories(asio BEFORE INTERFACE ${PROJECT_SOURCE_DIR}/Source/platform/wiiu/asio/include)
