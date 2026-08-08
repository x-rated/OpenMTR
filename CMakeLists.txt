# ============================================================================
#  CMakeLists.txt - build definition for OpenMTR
#  (static Qt on every platform: Windows exe / macOS .app bundle / Linux
#  AppImage - see .github/workflows/build.yml for how each Qt is built)
# ============================================================================

cmake_minimum_required(VERSION 3.22)

# Must be set before project(): the minimum macOS version the app targets,
# and the only architecture it's built for (Apple Silicon only — no Intel).
# 13.0 is what the official Qt 6.11 binaries are built for — every bundled
# framework and plugin declares minos 13. Declaring more than Qt needs just
# excludes working systems (Ventura); declaring less lets macOS launch a
# bundle whose Qt then refuses to load, killing the app at startup instead
# of showing a clean "requires macOS 13" dialog. The CI job re-checks this
# against every binary it ships (see .github/workflows/build.yml).
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum OS X version" FORCE)
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
# No Qt6Network: the one-shot GitHub release check (MainWindow::
# checkForUpdates()) shells out to the OS's own curl instead of linking
# a network/TLS stack into this binary - see the comment above that
# function for the fuller trade-off.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# DBus is Linux-only: used to watch org.freedesktop.portal.Settings for live
# light/dark and accent-colour changes, and for a one-shot accent-colour
# read (see MainWindow::onPortalSettingChanged() and
# linuxSystemAccentColor() in MainWindow.h), since
# Qt's own QStyleHints::colorSchemeChanged is unreliable on several
# distros/desktop environments (notably GNOME) - it may never fire at all
# even though the OS setting does change. Ships as part of the same qt6-base
# source package as Widgets, so no extra apt package is needed in CI.
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
endif()

# Every platform links a static, feature-trimmed Qt now (see
# .github/workflows/build.yml), so each one needs its QPA platform plugin
# pulled in explicitly - qt_import_plugins() can't discover it automatically
# for a static build the way it would for a dynamic one.
if(WIN32)
    qt_import_plugins(OpenMTR INCLUDE Qt6::QWindowsIntegrationPlugin)
elseif(APPLE)
    qt_import_plugins(OpenMTR INCLUDE Qt6::QCocoaIntegrationPlugin)
elseif(UNIX)
    # xcb only - covers plain X11 and XWayland, which is how the large
    # majority of desktop Linux sessions still run (including most "Wayland"
    # desktops, transparently through XWayland). A native-Wayland-only
    # session with no XWayland would need the separate qtwayland module
    # built and statically linked too, which pulls in its own build-time
    # deps (wayland-scanner, wayland-protocols, libxkbcommon) - left out
    # for now as a size/complexity trade-off; revisit if that turns out to
    # matter for real users.
    qt_import_plugins(OpenMTR INCLUDE Qt6::QXcbIntegrationPlugin)
endif()

# ---- Link libraries (Qt + the platform APIs the app uses) ------------------
target_link_libraries(OpenMTR PRIVATE
    Qt6::Widgets
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
        delayimp     # Required by /DELAYLOAD below
    )
    # Delay-load the networking DLLs: they're pulled in only when the code
    # actually calls into them (first ping / DNS lookup), instead of at
    # process start. Keeps the import table from showing ws2_32/Iphlpapi
    # as unconditional startup dependencies, which some AV ML classifiers
    # weight as a "reaches for the network immediately" signal.
    target_link_options(OpenMTR PRIVATE
        /DELAYLOAD:ws2_32.dll
        /DELAYLOAD:Iphlpapi.dll
    )
endif()

# ---- Target properties & compiler options ----------------------------------
# WIN32_EXECUTABLE: build a GUI app (no console window).
# MACOSX_BUNDLE: build a macOS app bundle (.app) instead of a bare binary.
set_target_properties(OpenMTR PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# Whole-program / link-time code generation (MSVC: /GL + /LTCG, GCC/Clang:
# equivalent LTO). Standard for professionally built release binaries;
# absence of it can leave the compiled code in an unoptimized shape that
# some AV ML classifiers weight as an "amateur toolchain" signal.
set_target_properties(OpenMTR PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)

# ---- macOS bundle identity & icon ------------------------------------------
# Without these, CMake's default Info.plist template ships placeholders that
# users actually see: CFBundleIdentifier "com.yourcompany.OpenMTR" and no
# Finder/Dock icon at all. The .icns is generated at build time from
# app_icon.png with the stock sips/iconutil tools (present on every macOS
# install, including CI runners).
#
# The full iconset is emitted up to 512@2x (1024 px). app_icon.png is
# currently a 256 px master, so the entries above it are upscales — the
# artwork is smooth gradient shapes with no fine detail or text, which
# survives that far better than a detailed icon would, and macOS otherwise
# upscales the 256 px entry itself for Launchpad, Get Info and large Finder
# icon views. Dropping a 1024 px app_icon.png in place makes every size
# natively crisp with no change here.
if(APPLE)
    set(APP_ICON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.png")
    set(APP_ICONSET  "${CMAKE_CURRENT_BINARY_DIR}/app_icon.iconset")
    set(APP_ICNS     "${CMAKE_CURRENT_BINARY_DIR}/app_icon.icns")

    # sips -z takes height then width; every entry here is square. The
    # @2x names are what iconutil expects — same pixel count as the plain
    # entry one step up, but tagged as the Retina variant of the smaller
    # logical size.
    set(_icns_cmds COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_ICONSET}")
    foreach(spec "16:icon_16x16" "32:icon_16x16@2x" "32:icon_32x32"
                 "64:icon_32x32@2x" "128:icon_128x128" "256:icon_128x128@2x"
                 "256:icon_256x256" "512:icon_256x256@2x" "512:icon_512x512"
                 "1024:icon_512x512@2x")
        string(REPLACE ":" ";" spec "${spec}")
        list(GET spec 0 px)
        list(GET spec 1 name)
        list(APPEND _icns_cmds
            COMMAND sips -z ${px} ${px} "${APP_ICON_SRC}" --out "${APP_ICONSET}/${name}.png")
    endforeach()

    add_custom_command(
        OUTPUT "${APP_ICNS}"
        ${_icns_cmds}
        COMMAND iconutil -c icns "${APP_ICONSET}" -o "${APP_ICNS}"
        DEPENDS "${APP_ICON_SRC}"
        COMMENT "Generating app_icon.icns from app_icon.png"
        VERBATIM
    )
    target_sources(OpenMTR PRIVATE "${APP_ICNS}")
    set_source_files_properties("${APP_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(OpenMTR PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER       "eu.slamb.OpenMTR"
        MACOSX_BUNDLE_BUNDLE_NAME          "OpenMTR"
        MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_ICON_FILE            "app_icon.icns"
    )
endif()

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

# Clang/GCC counterpart of the MSVC /W4 block below. Not /WX-equivalent
# (-Werror): the Windows build gates on its own warnings, and turning every
# future compiler's new diagnostics into a hard build failure on the two
# platforms whose toolchain version isn't pinned would break CI on unrelated
# upgrades.
if(NOT MSVC)
    target_compile_options(OpenMTR PRIVATE -Wall -Wextra)
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

    # /RELEASE: forces the linker to compute and set the PE header checksum
    # (some configurations otherwise leave it zero) — a lot of malware ships
    # with a missing/invalid checksum, so a correct one is a mild "properly
    # built" signal for AV heuristics.
    target_link_options(OpenMTR PRIVATE /RELEASE)

    # /PDBALTPATH: the CodeView debug directory embedded in the .exe itself
    # (not just the .pdb) normally records the absolute build-machine path
    # to the PDB, e.g. D:\a\OpenMTR\OpenMTR\build\OpenMTR.pdb — leaked even
    # though the PDB is never shipped. %_PDB% collapses that to just the
    # bare filename, removing the CI runner path from the shipped binary.
    target_link_options(OpenMTR PRIVATE /PDBALTPATH:%_PDB%)
endif()
