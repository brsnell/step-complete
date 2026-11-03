add_library(sc_render STATIC
  AisCompat.h
  OcctViewport.h
  OcctViewport.cpp
  SceneStyle.h
  SceneStyle.cpp
  SceneManager.h
  SceneManager.cpp
  WitnessPresentation.h
  WitnessPresentation.cpp
  MeasureTool.h
  MeasureTool.cpp
  PmiScene.h
  PmiScene.cpp
  PmiLabel.h
  PmiLabel.cpp
  PmiFont.h
  PmiFont.cpp
  SectionTool.h
  SectionTool.cpp
)
add_library(sc::render ALIAS sc_render)

# The PMI font (SIL OFL 1.1, see resources/fonts/OFL.txt and THIRD_PARTY_NOTICES.md) is
# embedded and registered with OCCT at run time, see PmiFont.h.
qt_add_resources(sc_render sc_render_fonts
  PREFIX "/fonts"
  BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
  FILES "${PROJECT_SOURCE_DIR}/resources/fonts/STIXTwoMath-Regular.ttf")

target_link_libraries(sc_render PUBLIC
  sc::core
  sc::occt_visual
  Qt6::Widgets
  Qt6::OpenGLWidgets)
sc_target_defaults(sc_render)
