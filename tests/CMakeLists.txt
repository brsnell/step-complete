find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
include(GoogleTest)

# --- Shared fixtures (synthetic STEP models) -------------------------------------
add_library(sc_test_support STATIC
  support/TestModels.h
  support/TestModels.cpp
)
target_include_directories(sc_test_support PUBLIC support)
target_link_libraries(sc_test_support PUBLIC sc::core)
sc_target_defaults(sc_test_support)

# --- Core unit tests (headless) -----------------------------------------------
add_executable(sc_core_tests
  core/test_units.cpp
  core/test_step_import.cpp
  core/test_real_models.cpp
  core/test_measure.cpp
  core/test_measurement.cpp
  core/test_pmi_text.cpp
)
target_link_libraries(sc_core_tests PRIVATE sc::core sc_test_support GTest::gtest_main)
sc_target_defaults(sc_core_tests)
gtest_discover_tests(sc_core_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "core.")

# --- Render smoke tests (need an OpenGL-capable display, e.g. Xvfb + Mesa) ------
add_executable(sc_render_tests
  render/render_test_main.cpp
  render/RenderTestUtils.h
  render/test_viewport_smoke.cpp
  render/test_scene_manager.cpp
  render/test_measure_tool.cpp
)
target_link_libraries(sc_render_tests PRIVATE sc::render sc_test_support Qt6::Test)
sc_target_defaults(sc_render_tests)
add_test(NAME render.viewport_smoke COMMAND sc_render_tests)
set_tests_properties(render.viewport_smoke PROPERTIES LABELS "render" TIMEOUT 120)
