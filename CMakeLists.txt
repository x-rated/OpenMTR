# ============================================================================
#  CMakeLists.txt - build definition for OpenMTR
#  (Windows static / macOS bundle / Linux AppImage)
# ============================================================================

cmake_minimum_required(VERSION 3.22)

# Must be set before project(): the minimum macOS version the app targets,
# and the only architecture it's built for (Apple Silicon only — no Intel).
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum OS X version" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures" FORCE)
endif()

project(OpenMTR VERSION 1.1.0 LANGUAGES CXX)

# ---- Language & Qt code generation -----------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---- Generated version header ----------------------------------------------
# Written to the build dir so the sources can #include "version.h".
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.h"
    "#pragma once\n"
    "#define OPENMTR_VERSION       \"${PROJECT_VERSION}\"\n"
    "#define OPENMTR_VERSION_TITLE \"OpenMTR\"\n"
)

# ---- Windows resource script (icon + version info) -------------------------
# Expand the @...@ placeholders in app.rc.in into app.rc. Windows-only: the
# .rc format isn't meaningful to other platforms' compilers.
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
endif()
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# ---- Dependencies ----------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# DBus is Linux-only: used to watch org.freedesktop.portal.Settings for live
# light/dark and accent-colour changes, and for a one-shot accent-colour
# read (see MainWindow::onPortalSettingChanged() and
# linuxSystemAccentColor() in MainWindow.h), since
# Qt's own QStyleHints::colorSchemeChanged is unreliable on several
# distros/desktop environments (notably GNOME) - it may never fire at all
# even though the OS setting does change. Ships as part of the same qt6-base
# source package as Widgets/Network, so no extra apt package is needed in CI.
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)

    # xcb/xcb-shape: used directly (no CMake config package for either, hence
    # pkg-config) to cut the click-through input shape for the frameless
    # window's transparent shadow gutter - see kShadowMargin and
    # MainWindow::updateLinuxInputShape(). X11/XWayland only, same as the
    # rest of that gutter-painting code; harmless to look for unconditionally
    # since a native-Wayland build just won't find an X11 session to use it
    # against at runtime.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB_SHAPE REQUIRED IMPORTED_TARGET xcb xcb-shape)
endif()

# ---- Executable ------------------------------------------------------------
qt_add_executable(OpenMTR
    main.cpp
    MainWindow.cpp
    MainWindow.h
    tracer.cpp
    tracer.h
    resources.qrc
)

if(WIN32)
    target_sources(OpenMTR PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

    # Static Qt build: pull in the platform plugin explicitly. (Not needed
    # on macOS, which links Qt dynamically and bundles the Cocoa plugin via
    # macdeployqt instead - see .github/workflows/build.yml.)
    qt_import_plugins(OpenMTR
        INCLUDE Qt6::QWindowsIntegrationPlugin
    )
endif()

# ---- Link libraries (Qt + the platform APIs the app uses) ------------------
target_link_libraries(OpenMTR PRIVATE
    Qt6::Widgets
    Qt6::Network
)

if(UNIX AND NOT APPLE)
    target_link_libraries(OpenMTR PRIVATE Qt6::DBus PkgConfig::XCB_SHAPE)
endif()

if(WIN32)
    target_link_libraries(OpenMTR PRIVATE
        ws2_32       # Winsock (address parsing, byte order)
        Iphlpapi     # IcmpSendEcho2 / Icmp6SendEcho2
        dwmapi       # Mica backdrop, dark titlebar, rounded corners
        dnsapi       # DnsQuery_W (ASN TXT lookups)
        comdlg32     # GetSaveFileNameW (export dialog)
    )
endif()

# ---- Target properties & compiler options ----------------------------------
# WIN32_EXECUTABLE: build a GUI app (no console window).
# MACOSX_BUNDLE: build a macOS app bundle (.app) instead of a bare binary.
set_target_properties(OpenMTR PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# ---- Linux install rules ----------------------------------------------------
# Only used for the AppImage build (see .github/workflows/build.yml): plain
# `install(TARGETS)` plus the desktop entry/icon that linuxdeploy expects to
# find under an AppDir's usr/{bin,share/applications,share/icons/...}. Inert
# on Windows/macOS, which package differently (static exe / .app bundle).
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS OpenMTR RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openmtr.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
        RENAME "openmtr.png")
endif()

if(MSVC)
    target_sources(OpenMTR PRIVATE app.manifest)
    # /W4 /WX: high warning level, warnings as errors.
    # /wd4100: unreferenced formal parameter. /wd4127: conditional is constant.
    target_compile_options(OpenMTR PRIVATE /W4 /WX
        /wd4100
        /wd4127
    )
    # Control Flow Guard. Besides being a standard exploit mitigation, its
    # presence sets the CFG bit in the PE's DLL characteristics, which is a
    # strong "benign" signal for ML-based AV classifiers (malware rarely ships
    # CFG) and cuts false-positive detections. Needs both the compile flag
    # (emit guard checks) and the link flag (emit guard tables + set the bit).
    target_compile_options(OpenMTR PRIVATE /guard:cf)
    target_link_options(OpenMTR PRIVATE /GUARD:CF)
endif()
