# Common warning and sanitizer flags, applied per target via sc_target_defaults().

function(sc_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    # OCCT headers trigger these at /W4.
    target_compile_options(${target} PRIVATE /wd4100 /wd4127 /wd4251 /wd4275 /wd4458)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    if(SC_WARNINGS_AS_ERRORS)
      # Deprecations in OCCT/Qt headers stay visible on other compilers but must not block builds.
      target_compile_options(${target} PRIVATE /WX /wd4996)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(SC_WARNINGS_AS_ERRORS)
      # Deprecations (OCCT 8 deprecates many 7.x APIs) are reported but must not block builds.
      target_compile_options(${target} PRIVATE -Werror -Wno-error=deprecated-declarations)
    endif()
    if(SC_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
  # Avoid clashes between Qt's 'signals'/'slots'/'emit' keywords and third-party headers.
  target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
  sc_vcpkg_qt_conf(${target})
endfunction()

# Qt from vcpkg on Windows (the 'qt' manifest feature): vcpkg copies Qt6Core.dll next
# to each executable, and Qt then looks for its plugins relative to that copy, finds
# no platform plugin and aborts (an error dialog that hangs headless test runs). A
# qt.conf beside the executable points Qt back at vcpkg's tree. Build tree only: the
# packaged app uses the official Qt and windeployqt.
function(sc_vcpkg_qt_conf target)
  get_target_property(type ${target} TYPE)
  if(NOT WIN32 OR NOT type STREQUAL "EXECUTABLE" OR NOT Qt6_DIR MATCHES "vcpkg_installed")
    return()
  endif()
  # vcpkg: <triplet>/share/Qt6 -> prefix <triplet>, plugins <triplet>[/debug]/Qt6/plugins
  get_filename_component(prefix "${Qt6_DIR}/../.." ABSOLUTE)
  file(GENERATE OUTPUT "$<TARGET_FILE_DIR:${target}>/qt.conf"
       CONTENT "[Paths]\nPrefix = ${prefix}$<$<CONFIG:Debug>:/debug>\nPlugins = Qt6/plugins\n")
endfunction()
