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
  SectionModel.h
  SectionModel.cpp
  SectionPanel.h
  SectionPanel.cpp
  AboutDialog.h
  AboutDialog.cpp
)
add_library(sc::app ALIAS sc_app)

target_link_libraries(sc_app PUBLIC sc::render sc::core Qt6::Widgets)
sc_target_defaults(sc_app)

# --- Application executable and its platform packaging bits -----------------
set(SC_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")
set(SC_ICON_DIR "${PROJECT_SOURCE_DIR}/resources/icons")
set(stepcomplete_platform_sources)

if(WIN32)
  # Executable icon + VERSIONINFO (version from PROJECT_VERSION).
  set(SC_ICO_PATH "${SC_PACKAGING_DIR}/windows/stepcomplete.ico")
  configure_file("${SC_PACKAGING_DIR}/windows/stepcomplete.rc.in"
                 "${CMAKE_CURRENT_BINARY_DIR}/stepcomplete.rc" @ONLY)
  list(APPEND stepcomplete_platform_sources "${CMAKE_CURRENT_BINARY_DIR}/stepcomplete.rc")
endif()

if(APPLE)
  set(SC_ICNS "${SC_PACKAGING_DIR}/macos/stepcomplete.icns")
  set_source_files_properties("${SC_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  list(APPEND stepcomplete_platform_sources "${SC_ICNS}")
endif()

qt_add_executable(stepcomplete WIN32 MACOSX_BUNDLE
  main.cpp
  ${stepcomplete_platform_sources}
)

# Window icon: all PNG sizes embedded as :/icons/stepcomplete-<size>.png (no QtSvg needed).
set(SC_ICON_SIZES 16 24 32 48 64 128 256 512)
set(SC_ICON_FILES)
foreach(size IN LISTS SC_ICON_SIZES)
  list(APPEND SC_ICON_FILES "${SC_ICON_DIR}/png/stepcomplete-${size}.png")
endforeach()
qt_add_resources(stepcomplete stepcomplete_icons
  PREFIX "/icons"
  BASE "${SC_ICON_DIR}/png"
  FILES ${SC_ICON_FILES}
)
set_target_properties(stepcomplete PROPERTIES
  OUTPUT_NAME "StepComplete"
  MACOSX_BUNDLE_BUNDLE_NAME "STEP Complete"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.stepcomplete.StepComplete"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_ICON_FILE "stepcomplete.icns"
  MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 STEP Complete contributors. MIT License."
  MACOSX_BUNDLE_INFO_PLIST "${SC_PACKAGING_DIR}/macos/Info.plist.in")

target_link_libraries(stepcomplete PRIVATE sc::app sc::render sc::core)
sc_target_defaults(stepcomplete)

# Full license texts shown in Help > About > Licenses (LGPL compliance). The OFL text
# stays next to the font it covers and is added under the same prefix.
qt_add_resources(sc_app sc_app_licenses
  PREFIX "/licenses"
  BASE "${PROJECT_SOURCE_DIR}/resources/licenses"
  FILES
    "${PROJECT_SOURCE_DIR}/resources/licenses/MIT.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/LGPL-2.1.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/OCCT_LGPL_EXCEPTION.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/LGPL-3.0.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/GPL-3.0.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/FTL.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/fontconfig.txt"
    "${PROJECT_SOURCE_DIR}/resources/licenses/zlib.txt")
qt_add_resources(sc_app sc_app_font_license
  PREFIX "/licenses"
  BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
  FILES "${PROJECT_SOURCE_DIR}/resources/fonts/OFL.txt")
