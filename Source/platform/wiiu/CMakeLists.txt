include(functions/devilutionx_library)

add_devilutionx_object_library(libdevilutionx_wiiu
  stub.cpp
  init.cpp
  keyboard.cpp
)

target_link_libraries(libdevilutionx_wiiu PUBLIC
  DevilutionX::SDL
)