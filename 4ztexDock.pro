QT += core gui widgets dbus network gui-private
CONFIG += c++17 link_pkgconfig
CONFIG -= app_bundle

# xcb-ewmh: X11 dock window setup (Plasma 6 X11 / Plasma 5).
PKGCONFIG += xcb xcb-ewmh

TARGET = 4ztexDock
TEMPLATE = app

HEADERS += src/kwinbridge.h src/traybridge.h src/notificationserver.h src/icons.h src/audio_parser.h src/sysctl.h src/config.h src/x11support.h src/kdetools.h
SOURCES += src/main.cpp src/kwinbridge.cpp src/traybridge.cpp src/notificationserver.cpp src/icons.cpp src/audio_parser.cpp src/sysctl.cpp src/config.cpp src/x11support.cpp src/kdetools.cpp

LIBS += -lLayerShellQtInterface

# i18n: lupdate6 ile bu .ts dosyalarına string'ler extract edilir, çeviri
# yazıldıktan sonra lrelease6 ile .qm üretilir, translations.qrc bunları
# binary'ye gömer. Source language Türkçe (sourceLanguage="tr_TR").
TRANSLATIONS += \
    translations/4ztexDock_tr.ts \
    translations/4ztexDock_en.ts

# .qm dosyaları binary'nin içine gömülür → kurulumda ayrı dosya gerekmez.
RESOURCES += translations.qrc

DISTFILES += style/dock.qss $$TRANSLATIONS
