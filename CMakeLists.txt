cmake_minimum_required(VERSION 3.28)
project(epaper)

include(FeatureSummary)

find_package(QT NAMES Qt6 COMPONENTS Core REQUIRED HINTS $ENV{Qt6_DIR})
if (QT_VERSION VERSION_LESS 6.10)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Test DeviceDiscoverySupportPrivate InputSupportPrivate)
else ()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Test DeviceDiscoverySupportPrivate InputSupportPrivate GuiPrivate)
endif ()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

add_library(epaper
    SHARED

    epaperbackingstore.cpp
    epaperevdevkeyboardhandler.cpp
    epaperevdevkeyboardmanager.cpp
    epaperintegration.cpp
    epaperplatformtheme.cpp
    main.cpp

    epaperbackingstore.h
    epaperevdevkeyboardhandler.h
    epaperevdevkeyboardmanager.h
    epaperintegration.h
    epaperplatformtheme.h

    map/epaperevdevkeyboardmap_de.h
    map/epaperevdevkeyboardmap_dk.h
    map/epaperevdevkeyboardmap_es.h
    map/epaperevdevkeyboardmap_fr.h
    map/epaperevdevkeyboardmap_no.h
    map/epaperevdevkeyboardmap_se.h
    map/epaperevdevkeyboardmap_uk.h
    map/epaperevdevkeyboardmap_us_rm.h
)

target_include_directories(epaper PUBLIC .)

target_link_libraries(epaper
    Qt6::Core
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::DeviceDiscoverySupportPrivate
    Qt6::InputSupportPrivate
)
set(QT6_PLATFORMS_PLUGIN_DIR lib/qt6/plugins/platforms CACHE STRING "Plugin install dir")
install(TARGETS epaper DESTINATION ${QT6_PLATFORMS_PLUGIN_DIR})

