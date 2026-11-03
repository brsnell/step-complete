# Everything except main.cpp lives in a static library so tests can link the widgets.
add_library(sc_app STATIC
  MainWindow.h
  MainWindow.cpp
  ImportJob.h
  ImportJob.cpp
  AssemblyTreeModel.h
  AssemblyTreeModel.cpp
  PropertiesPanel.h
  PropertiesPanel.cpp
  MeasurementsPanel.h
  MeasurementsPanel.cpp
  PmiTreeModel.h
  PmiTreeModel.cpp
  PmiPanel.h
  PmiPanel.cpp
  ExportImageDialog.h
  ExportImageDialog.cpp
  PreferencesDialog.h
  PreferencesDialog.cpp
)
add_library(sc::app ALIAS sc_app)

target_link_libraries(sc_app PUBLIC sc::render sc::core Qt6::Widgets)
sc_target_defaults(sc_app)

qt_add_executable(stepcomplete WIN32 MACOSX_BUNDLE
  main.cpp
)

set_target_properties(stepcomplete PROPERTIES
  OUTPUT_NAME "StepComplete"
  MACOSX_BUNDLE_BUNDLE_NAME "STEP Complete"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.stepcomplete.StepComplete"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

target_link_libraries(stepcomplete PRIVATE sc::app sc::render sc::core)
sc_target_defaults(stepcomplete)
