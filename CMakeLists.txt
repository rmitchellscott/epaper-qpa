cmake_minimum_required(VERSION 3.25)
project(epaper)

include(FeatureSummary)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test DeviceDiscoverySupportPrivate InputSupportPrivate)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)
include(CTest)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

add_library(epaper
    SHARED

    epaperbackingstore.cpp
    epaperevdevkeyboardhandler.cpp
    epaperevdevkeyboardmanager.cpp
    epaperevdevtouchhandler.cpp
    epaperevdevtouchmanager.cpp
    epaperevdevtouchscreendata.cpp
    epaperevdevutil.cpp
    epaperintegration.cpp
    epaperplatformtheme.cpp
    main.cpp

    epaperbackingstore.h
    epaperevdevkeyboardhandler.h
    epaperevdevkeyboardmanager.h
    epaperevdevtouchhandler.h
    epaperevdevtouchmanager.h
    epaperevdevtouchscreendata.h
    epaperevdevutil.h
    epaperintegration.h
    epaperplatformtheme.h

    map/epaperevdevkeyboardmap_de.h
    map/epaperevdevkeyboardmap_dk.h
    map/epaperevdevkeyboardmap_es.h
    map/epaperevdevkeyboardmap_fr.h
    map/epaperevdevkeyboardmap_it.h
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

add_executable(tst_touch tests/tst_touch.cpp)
target_link_libraries(tst_touch PUBLIC epaper Qt6::Test)
add_test(NAME tst_touch COMMAND tst_touch)
