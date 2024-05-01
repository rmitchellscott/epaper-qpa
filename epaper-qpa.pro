TARGET = epaper
TEMPLATE = lib

CONFIG += plugin
CONFIG += qpa/genericunixfontdatabase c++17
QT += core-private gui-private input_support-private

SOURCES = \
    epaperbackingstore.cpp \
    epaperevdevkeyboardhandler.cpp \
    epaperevdevkeyboardmanager.cpp \
    epaperevdevtouchhandler.cpp \
    epaperevdevtouchmanager.cpp \
    epaperevdevutil.cpp \
    epaperintegration.cpp \
    main.cpp


HEADERS = \
    epaperbackingstore.h \
    epaperevdevkeyboardhandler.h \
    epaperevdevkeyboardmanager.h \
    epaperevdevtouchhandler.h \
    epaperevdevtouchmanager.h \
    epaperevdevutil.h \
    epaperintegration.h

HEADERS += \
    map/epaperevdevkeyboardmap_de.h \
    map/epaperevdevkeyboardmap_dk.h \
    map/epaperevdevkeyboardmap_es.h \
    map/epaperevdevkeyboardmap_fr.h \
    map/epaperevdevkeyboardmap_it.h \
    map/epaperevdevkeyboardmap_no.h \
    map/epaperevdevkeyboardmap_se.h \
    map/epaperevdevkeyboardmap_uk.h \
    map/epaperevdevkeyboardmap_us_rm.h \

OTHER_FILES += minimal.json

target.path += $$[QT_INSTALL_PLUGINS]/platforms
INSTALLS += target

