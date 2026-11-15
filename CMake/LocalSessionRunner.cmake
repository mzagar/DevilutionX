# No-UI local-session runner (see test/local_session_runner.cpp).
#
# It links the real engine library so it boots, loads and ticks exactly what the
# game does. Intentionally not part of BUILD_TESTING: cross-compiles force
# BUILD_TESTING=OFF, but the console build must still run session scenarios.

add_executable(local_session_runner test/local_session_runner.cpp)
set_target_properties(local_session_runner PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

target_link_dependencies(local_session_runner PRIVATE libdevilutionx)

if(NOT USE_SDL1 AND NOT UWP_LIB)
  target_link_libraries(local_session_runner PUBLIC ${SDL2_MAIN})
endif()
