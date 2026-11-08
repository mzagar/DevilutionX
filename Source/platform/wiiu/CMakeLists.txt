include(functions/devilutionx_library)

set(wiiu_platform_sources
  stub.cpp
  init.cpp
  keyboard.cpp
  network.cpp
)

# asio needs a few POSIX declarations wut does not ship; the shims live in
# asio/include and are only compiled when networking is enabled.
if(NOT NONET)
  list(APPEND wiiu_platform_sources
    asio/net/if.c
    asio/pipe.c
    asio/pause.c
    asio/sys/signal.c
    asio/sys/socket.c
    asio/sys/uio.c
  )
endif()

add_devilutionx_object_library(libdevilutionx_wiiu ${wiiu_platform_sources})

# Linking asio is what puts Source/platform/wiiu/asio/include on this target's
# include path (see CMake/platforms/wiiu/asio_defs.cmake).
if(NOT NONET)
  if(NOT DISABLE_TCP)
    target_link_libraries(libdevilutionx_wiiu PUBLIC asio)
  endif()
  if(PACKET_ENCRYPTION)
    target_link_libraries(libdevilutionx_wiiu PUBLIC sodium)
  endif()
endif()

target_link_libraries(libdevilutionx_wiiu PUBLIC
  DevilutionX::SDL
)
