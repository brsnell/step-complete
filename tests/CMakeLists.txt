find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)
include(GoogleTest)

# --- Shared fixtures (synthetic STEP models) -------------------------------------
add_library(sc_test_support STATIC
  support/TestModels.h
  support/TestModels.cpp
  support/PmiFixture.h
  support/PmiFixture.cpp
)
target_include_directories(sc_test_support PUBLIC support)
target_link_libraries(sc_test_support PUBLIC sc::core)
sc_target_defaults(sc_test_support)

# --- Core unit tests (headless) -----------------------------------------------
find_package(ZLIB REQUIRED) # the compressed-STEP tests build fixtures with zlib
add_executable(sc_core_tests
  core/test_units.cpp
  core/test_step_import.cpp
  core/test_real_models.cpp
  core/test_measure.cpp
  core/test_measurement.cpp
  core/test_pmi_text.cpp
  core/test_pmi_model.cpp
  core/test_compressed_step.cpp
)
target_link_libraries(sc_core_tests PRIVATE sc::core sc_test_support ZLIB::ZLIB GTest::gtest_main)
sc_target_defaults(sc_core_tests)
gtest_discover_tests(sc_core_tests DISCOVERY_MODE PRE_TEST TEST_PREFIX "core.")

# --- Render smoke tests (need an OpenGL-capable display, e.g. Xvfb + Mesa) ------
add_executable(sc_render_tests
  render/render_test_main.cpp
  render/RenderTestUtils.h
  render/test_viewport_smoke.cpp
  render/test_scene_manager.cpp
  render/test_measure_tool.cpp
  render/test_pmi_scene.cpp
  render/test_section_tool.cpp
  render/test_view_settings.cpp
)
target_link_libraries(sc_render_tests PRIVATE sc::render sc_test_support Qt6::Test)
sc_target_defaults(sc_render_tests)
add_test(NAME render.viewport_smoke COMMAND sc_render_tests)
set_tests_properties(render.viewport_smoke PROPERTIES LABELS "render" TIMEOUT 120)

# Renders the NIST PMI models (iso + saved views) into SC_RENDER_DUMP_DIR; CI uploads them.
add_executable(sc_pmi_snapshots
  render/pmi_snapshots.cpp
  render/RenderTestUtils.h
)
target_link_libraries(sc_pmi_snapshots PRIVATE sc::render sc_test_support Qt6::Test)
sc_target_defaults(sc_pmi_snapshots)
add_test(NAME render.pmi_snapshots COMMAND sc_pmi_snapshots)
set_tests_properties(render.pmi_snapshots PROPERTIES LABELS "render" TIMEOUT 300)

# --- App widget tests (offscreen Qt platform: no display or OpenGL needed) -----
add_executable(sc_app_tests
  app/app_test_main.cpp
  app/test_pmi_panel.cpp
  app/test_section_panel.cpp
  app/test_about_dialog.cpp
)
target_link_libraries(sc_app_tests PRIVATE sc::app sc_test_support Qt6::Test)
sc_target_defaults(sc_app_tests)
add_test(NAME app.pmi_panel COMMAND sc_app_tests)
set_tests_properties(app.pmi_panel PROPERTIES TIMEOUT 120)

add_executable(sc_preferences_tests
  app/test_preferences_dialog.cpp
)
target_link_libraries(sc_preferences_tests PRIVATE sc::app Qt6::Test)
sc_target_defaults(sc_preferences_tests)
add_test(NAME app.preferences COMMAND sc_preferences_tests)
set_tests_properties(app.preferences PROPERTIES TIMEOUT 120)
