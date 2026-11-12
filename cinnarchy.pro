QT += core gui qml quick quickcontrols2 dbus multimedia network
CONFIG += c++17 release link_pkgconfig
PKGCONFIG += gio-2.0 x11 xrandr xext
TARGET = cinnarchy
TEMPLATE = app
HEADERS += src/backend.h src/settings.h src/theme.h src/translator.h src/wallpaper.h src/filepicker.h src/generator.h src/fetcher.h
SOURCES += src/main.cpp src/backend.cpp src/settings.cpp src/theme.cpp src/translator.cpp src/wallpaper.cpp src/filepicker.cpp src/generator.cpp src/fetcher.cpp
RESOURCES += src/resources.qrc
