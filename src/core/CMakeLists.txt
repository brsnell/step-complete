configure_file(Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h @ONLY)

add_library(sc_core STATIC
  OccHandle.h
  OccCompat.h
  Units.h
  Units.cpp
  OcctInfo.h
  OcctInfo.cpp
  Document.h
  Document.cpp
  StepImporter.h
  StepImporter.cpp
  AssemblyIndex.h
  AssemblyIndex.cpp
  Measure.h
  Measure.cpp
)
add_library(sc::core ALIAS sc_core)

# Core must stay free of Qt so it can be unit tested headless and reused by CLI tools.
# It links OCCT's visualization toolkits only for meshing parameters shared with the viewer.
target_include_directories(sc_core PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(sc_core PUBLIC sc::occt_foundation sc::occt_data sc::occt_visual)
sc_target_defaults(sc_core)
