cmake_minimum_required(VERSION 3.28)
project(epaper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(Qt6 COMPONENTS DeviceDiscoverySupportPrivate InputSupportPrivate)
add_definitions(-DQT_DEPRECATED_WARNINGS)

set(CMAKE_AUTOMOC ON)

qt_add_plugin(epaper SHARED
    main.cpp
    epaperintegration.cpp
    epaperbackingstore.cpp
    epaperevdevkeyboardhandler.cpp
    epaperevdevkeyboardmanager.cpp
    epaperplatformtheme.cpp
)

target_link_libraries(epaper PRIVATE
    Qt::CorePrivate
    Qt::GuiPrivate
    Qt::DeviceDiscoverySupportPrivate
    Qt::InputSupportPrivate)

install(TARGETS epaper DESTINATION ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms)

feature_summary(WHAT ALL)
