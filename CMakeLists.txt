cmake_minimum_required(VERSION 3.22)
project(OpenMTR VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

qt_add_executable(OpenMTR
    main.cpp
    MainWindow.cpp
    MainWindow.h
    OpenMTRNet.cpp
    OpenMTRNet.h
    resources.qrc
    app.rc
)

# Statický Qt: musíme explicitně importovat platform + style plugin
qt_import_plugins(OpenMTR
    INCLUDE Qt6::QWindowsIntegrationPlugin
    INCLUDE Qt6::QModernWindowsStylePlugin
    INCLUDE Qt6::QSvgPlugin
)

target_link_libraries(OpenMTR PRIVATE
    Qt6::Widgets
    Qt6::Svg
    ws2_32
    Iphlpapi
)

# Windows subsystem (žádná konzole) + manifest
set_target_properties(OpenMTR PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Embed app.manifest
if(MSVC)
    target_sources(OpenMTR PRIVATE app.manifest)
endif()
