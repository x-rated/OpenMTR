# ============================================================================
#  CMakeLists.txt — build definition for OpenMTR (static Qt 6 / Windows)
# ============================================================================

cmake_minimum_required(VERSION 3.22)
project(OpenMTR VERSION 1.2.0 LANGUAGES CXX)

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
# Expand the @...@ placeholders in app.rc.in into app.rc.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    @ONLY
)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# ---- Dependencies ----------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ---- Executable ------------------------------------------------------------
qt_add_executable(OpenMTR
    main.cpp
    MainWindow.cpp
    MainWindow.h
    tracer.cpp
    tracer.h
    resources.qrc
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
)

# Static Qt build: pull in the platform + ICO image plugins explicitly.
qt_import_plugins(OpenMTR
    INCLUDE Qt6::QWindowsIntegrationPlugin
    INCLUDE Qt6::QICOPlugin
)

# ---- Link libraries (Qt + the Win32 APIs the app uses) ---------------------
target_link_libraries(OpenMTR PRIVATE
    Qt6::Widgets
    ws2_32       # Winsock (address parsing, byte order)
    Iphlpapi     # IcmpSendEcho2 / Icmp6SendEcho2
    dwmapi       # Mica backdrop, dark titlebar, rounded corners
    dnsapi       # DnsQuery_W (ASN TXT lookups)
    comdlg32     # GetSaveFileNameW (export dialog)
)

# ---- Target properties & compiler options ----------------------------------
# WIN32_EXECUTABLE: build a GUI app (no console window).
set_target_properties(OpenMTR PROPERTIES
    WIN32_EXECUTABLE TRUE
)

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
