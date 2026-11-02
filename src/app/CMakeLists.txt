qt_add_executable(stepcomplete WIN32 MACOSX_BUNDLE
  main.cpp
  MainWindow.h
  MainWindow.cpp
)

set_target_properties(stepcomplete PROPERTIES
  OUTPUT_NAME "StepComplete"
  MACOSX_BUNDLE_BUNDLE_NAME "STEP Complete"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.stepcomplete.StepComplete"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_link_libraries(stepcomplete PRIVATE sc::render sc::core)
sc_target_defaults(stepcomplete)
