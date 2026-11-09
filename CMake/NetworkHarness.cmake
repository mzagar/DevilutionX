# No-UI network peer harness (see test/network_peer.cpp).
#
# It links the real engine library so that it uses exactly the same provider,
# packet codec and engine globals as the game. It is intentionally not part of
# BUILD_TESTING: cross-compiles force BUILD_TESTING=OFF, but the console peer
# must still be buildable.

if(NONET OR DISABLE_TCP)
  message(FATAL_ERROR "BUILD_NETWORK_HARNESS requires NONET=OFF and DISABLE_TCP=OFF")
endif()

add_executable(network_peer test/network_peer.cpp)
set_target_properties(network_peer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_dependencies(network_peer PRIVATE libdevilutionx)

if(NOT USE_SDL1 AND NOT UWP_LIB)
  target_link_libraries(network_peer PUBLIC ${SDL2_MAIN})
endif()
