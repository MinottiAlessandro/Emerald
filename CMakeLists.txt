cmake_minimum_required(VERSION 3.21)
project(Emerald VERSION 1.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Core is pulled in by Widgets, but naming it explicitly gives us Qt's
# deployment API (qt_generate_deploy_app_script).
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Svg)

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
  src/core/MascotSeed.h
  src/ui/MainWindow.h
  src/ui/MainWindow.cpp
  src/ui/MarkdownEditor.h
  src/ui/MarkdownEditor.cpp
  src/ui/Mascot.h
  src/ui/Mascot.cpp
  src/ui/MascotCatalog.h
  src/ui/MascotCatalog.cpp
  src/ui/MarkdownHighlighter.h
  src/ui/MarkdownHighlighter.cpp
  src/ui/MathRender.h
  src/ui/MathRender.cpp
  src/ui/SearchPopup.h
  src/ui/SearchPopup.cpp
  src/ui/Updater.h
  src/ui/Updater.cpp
  resources/resources.qrc
  ${EMERALD_PLATFORM_SOURCES}
)

target_include_directories(emerald PRIVATE src)
target_link_libraries(emerald PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg)
# Surface the project version to the app (shown in the sidebar footer, used by
# the update check to compare against the latest GitHub release).
target_compile_definitions(emerald PRIVATE EMERALD_VERSION="${PROJECT_VERSION}")

if(APPLE)
  set_target_properties(emerald PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST       "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME      "Emerald"
    MACOSX_BUNDLE_GUI_IDENTIFIER   "app.emerald.Emerald"
    MACOSX_BUNDLE_BUNDLE_VERSION   "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE        "emerald.icns"
    MACOSX_BUNDLE_COPYRIGHT        "© 2026 Alessandro Minotti — MIT License"
    # Qt is referenced as @rpath/QtFoo.framework/...; the frameworks land in
    # Contents/Frameworks (@executable_path/../Frameworks). CMake's install step
    # strips the build rpath, and macdeployqt doesn't reliably re-add one to the
    # main executable — leaving every @rpath dangling ("Library not loaded:
    # @rpath/QtWidgets... no LC_RPATH's found"). Bake the rpath in ourselves so
    # the bundle is self-locating regardless of macdeployqt's behaviour. This one
    # rpath on the executable also resolves the frameworks'/plugins' own @rpath
    # cross-references, since the main image's rpaths are in scope process-wide.
    INSTALL_RPATH                  "@executable_path/../Frameworks"
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

# macOS: macdeployqt rewrites the Qt library load paths after the linker's
# automatic ad-hoc signature, which invalidates it. Apple Silicon then refuses
# to run the bundle and Gatekeeper reports it as "damaged". Re-apply a valid
# ad-hoc signature on the fully-assembled bundle (this install step runs after
# the deploy script above), which downgrades the failure to the normal unsigned
# "unidentified developer" prompt where right-click -> Open works.
# Proper notarization still needs an Apple Developer ID; this is the free best-effort.
if(APPLE)
  install(CODE [[
    file(GLOB _emerald_app "${CMAKE_INSTALL_PREFIX}/*.app")
    if(_emerald_app)
      message(STATUS "Ad-hoc signing ${_emerald_app}")
      execute_process(
        COMMAND codesign --force --deep --sign - "${_emerald_app}"
        RESULT_VARIABLE _codesign_rc)
      if(NOT _codesign_rc EQUAL 0)
        message(WARNING "ad-hoc codesign failed (code ${_codesign_rc}); bundle left unsigned")
      endif()
    else()
      message(WARNING "no .app found under ${CMAKE_INSTALL_PREFIX} to sign")
    endif()
  ]])
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
# Stable, version-less archive names. The README links to
# /releases/latest/download/<name>, which GitHub always redirects to the newest
# release's asset of that name — so the download links never need bumping. The
# version still lives in the release tag/title and the package metadata.
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_FILE_NAME "Emerald-win64")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_PACKAGE_FILE_NAME "Emerald-macOS")
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_PACKAGE_FILE_NAME "Emerald-linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()
include(CPack)
