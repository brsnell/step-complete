add_library(sc_render STATIC
  OcctViewport.h
  OcctViewport.cpp
  SceneStyle.h
  SceneStyle.cpp
)
add_library(sc::render ALIAS sc_render)

target_link_libraries(sc_render PUBLIC
  sc::core
  sc::occt_visual
  Qt6::Widgets
  Qt6::OpenGLWidgets)
sc_target_defaults(sc_render)
