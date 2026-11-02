find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
include(GoogleTest)

# --- Core unit tests (headless) -----------------------------------------------
add_executable(sc_core_tests
  core/test_units.cpp
)
target_link_libraries(sc_core_tests PRIVATE sc::core GTest::gtest_main)
sc_target_defaults(sc_core_tests)
gtest_discover_tests(sc_core_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "core.")

# --- Render smoke tests (need an OpenGL-capable display, e.g. Xvfb + Mesa) ------
add_executable(sc_render_tests
  render/test_viewport_smoke.cpp
)
target_link_libraries(sc_render_tests PRIVATE sc::render Qt6::Test)
sc_target_defaults(sc_render_tests)
add_test(NAME render.viewport_smoke COMMAND sc_render_tests)
set_tests_properties(render.viewport_smoke PROPERTIES LABELS "render" TIMEOUT 120)
