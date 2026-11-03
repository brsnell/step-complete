# Install rules: executable, license texts and (Linux) desktop integration files.
# Windows and macOS packages are assembled from `cmake --install` output by the scripts in packaging/.
include(GNUInstallDirs)

set(SC_DOC_FILES
  "${PROJECT_SOURCE_DIR}/LICENSE"
  "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
  "${PROJECT_SOURCE_DIR}/resources/fonts/OFL.txt")
file(GLOB SC_LICENSE_TEXTS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/licenses/*.txt")
list(APPEND SC_DOC_FILES ${SC_LICENSE_TEXTS})

if(APPLE)
  install(TARGETS stepcomplete BUNDLE DESTINATION .)
  install(FILES ${SC_DOC_FILES}
          DESTINATION "StepComplete.app/Contents/Resources/licenses")
elseif(WIN32)
  # Flat layout (exe at the install root) so windeployqt output sits next to it.
  install(TARGETS stepcomplete RUNTIME DESTINATION .)
  install(FILES ${SC_DOC_FILES} DESTINATION licenses)
else()
  install(TARGETS stepcomplete RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES ${SC_DOC_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/stepcomplete")

  set(SC_APP_ID "org.stepcomplete.StepComplete")
  set(SC_LINUX_DIR "${PROJECT_SOURCE_DIR}/packaging/linux")
  configure_file("${SC_LINUX_DIR}/${SC_APP_ID}.metainfo.xml.in"
                 "${CMAKE_BINARY_DIR}/${SC_APP_ID}.metainfo.xml" @ONLY)

  install(FILES "${SC_LINUX_DIR}/${SC_APP_ID}.desktop"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  install(FILES "${CMAKE_BINARY_DIR}/${SC_APP_ID}.metainfo.xml"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
  install(FILES "${SC_LINUX_DIR}/stepcomplete-mime.xml"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages"
          RENAME "stepcomplete.xml")
  foreach(size IN ITEMS 16 24 32 48 64 128 256 512)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/png/stepcomplete-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME "stepcomplete.png")
  endforeach()
  install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/stepcomplete.svg"
          DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
