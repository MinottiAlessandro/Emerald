cmake_minimum_required(VERSION 3.21)
project(Emerald VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Core is pulled in by Widgets, but naming it explicitly gives us Qt's
# deployment API (qt_generate_deploy_app_script).
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# --- Platform icons -------------------------------------------------------
# Pre-generated and committed (see packaging/macos/make_icns.py and the .ico
# built with ImageMagick) so ordinary builds need no image tooling.
set(EMERALD_ICON_ICO  "${CMAKE_CURRENT_SOURCE_DIR}/resources/emerald.ico")
set(EMERALD_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/emerald.icns")

set(EMERALD_PLATFORM_SOURCES)
if(WIN32)
  # project(... LANGUAGES CXX) doesn't enable RC, so turn it on to compile the
  # icon resource (rc.exe under MSVC, windres under MinGW).
  enable_language(RC)
  # configure_file bakes the absolute icon path (forward slashes) into the .rc
  # so the resource compiler finds it from any working directory.
  configure_file(resources/emerald.rc.in "${CMAKE_CURRENT_BINARY_DIR}/emerald.rc" @ONLY)
  list(APPEND EMERALD_PLATFORM_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/emerald.rc")
elseif(APPLE)
  # Ship the .icns inside the bundle's Resources/ folder.
  list(APPEND EMERALD_PLATFORM_SOURCES "${EMERALD_ICON_ICNS}")
  set_source_files_properties("${EMERALD_ICON_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
endif()

# WIN32: GUI subsystem on Windows (no stray console window behind the app).
# MACOSX_BUNDLE: produce a proper .app bundle on macOS. Both flags are ignored
# on the platforms they don't apply to.
add_executable(emerald WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/core/Note.h
  src/core/Vault.h
  src/core/Vault.cpp
  src/core/SearchIndex.h
  src/core/SearchIndex.cpp
  src/core/WikiLink.h
  src/core/WikiLink.cpp
  src/ui/MainWindow.h
  src/ui/MainWindow.cpp
  src/ui/MarkdownEditor.h
  src/ui/MarkdownEditor.cpp
  src/ui/MarkdownHighlighter.h
  src/ui/MarkdownHighlighter.cpp
  src/ui/SearchPopup.h
  src/ui/SearchPopup.cpp
  resources/resources.qrc
  ${EMERALD_PLATFORM_SOURCES}
)

target_include_directories(emerald PRIVATE src)
target_link_libraries(emerald PRIVATE Qt6::Widgets)

if(APPLE)
  set_target_properties(emerald PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST       "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME      "Emerald"
    MACOSX_BUNDLE_GUI_IDENTIFIER   "app.emerald.Emerald"
    MACOSX_BUNDLE_BUNDLE_VERSION   "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE        "emerald.icns"
    MACOSX_BUNDLE_COPYRIGHT        "© 2026 Emerald"
  )
endif()

# --- Install rules --------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS emerald
  BUNDLE  DESTINATION .
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Linux desktop integration: menu entry + themed icons.
if(UNIX AND NOT APPLE)
  install(FILES packaging/linux/emerald.desktop
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
  foreach(size 128 256 512)
    install(FILES "packaging/linux/icons/emerald-${size}.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps"
            RENAME emerald.png)
  endforeach()
endif()

# --- Bundle the Qt runtime ------------------------------------------------
# Windows/macOS: Qt's deployment API wraps windeployqt / macdeployqt to fold the
# Qt libraries and plugins into the install tree (or .app), producing a
# self-contained, relocatable result on `cmake --install`.
# Linux: Qt's deploy-into-prefix is experimental and brittle with third-party
# plugins, so we keep `cmake --install` a clean system-style install and ship a
# portable AppImage via packaging/linux/make_appimage.sh (linuxdeploy) instead.
if(WIN32 OR APPLE)
  qt_generate_deploy_app_script(
    TARGET emerald
    OUTPUT_SCRIPT EMERALD_DEPLOY_SCRIPT
    NO_UNSUPPORTED_PLATFORM_ERROR
  )
  install(SCRIPT "${EMERALD_DEPLOY_SCRIPT}")
endif()

# --- Packaging (cpack) ----------------------------------------------------
# `cpack` after an install produces a distributable archive/installer.
# Generators: ZIP (Windows), drag-install .dmg (macOS), .tar.gz (Linux).
set(CPACK_PACKAGE_NAME "Emerald")
set(CPACK_PACKAGE_VENDOR "Emerald")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A minimal Obsidian-like Markdown note editor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Emerald")
set(CPACK_PACKAGE_EXECUTABLES "emerald" "Emerald")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
