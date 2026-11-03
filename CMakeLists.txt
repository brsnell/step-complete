cmake_minimum_required(VERSION 3.21)

project(StepComplete
  VERSION 0.1.0
  DESCRIPTION "Cross-platform STEP viewer with measurement and PMI"
  HOMEPAGE_URL "https://github.com/brsnell/step-complete"
  LANGUAGES CXX)

if(WIN32)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS 12 Monterey is the newest macOS that 2016-era Macs can run.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(SC_BUILD_TESTS "Build unit and render tests" ON)
option(SC_ENABLE_SANITIZERS "Build with ASan/UBSan (GCC/Clang only)" OFF)
option(SC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SC_BUILD_TOOLS "Build developer tools (benchmark model generator)" OFF)
set(SC_RELEASE_DATE "2026-11-03" CACHE STRING "Release date (YYYY-MM-DD) written to the AppStream metainfo")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerSettings)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Qt: 6.8 LTS is what we ship (it still supports macOS 12). 6.4 is accepted so
# developers can build against distro packages (e.g. Ubuntu 24.04).
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
qt_standard_project_setup()

# OCCT: 8.0.x is what we ship. 7.6 is accepted for distro builds.
find_package(OpenCASCADE CONFIG REQUIRED)
include(OcctToolkits)

if(SC_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/render)
add_subdirectory(src/app)

if(SC_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(SC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

include(InstallRules)

message(STATUS "STEP Complete ${PROJECT_VERSION}: Qt ${Qt6_VERSION}, OCCT ${OpenCASCADE_VERSION}")
