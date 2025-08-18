QT -= gui

TARGET = $$qtLibraryTarget(nymea_zwavepluginopenzwave)
TEMPLATE = lib

greaterThan(QT_MAJOR_VERSION, 5) {
    message("Building using Qt6 support")
    CONFIG *= c++17
    QMAKE_LFLAGS *= -std=c++17
    QMAKE_CXXFLAGS *= -std=c++17
} else {
    message("Building using Qt5 support")
    CONFIG *= c++11
    QMAKE_LFLAGS *= -std=c++11
    QMAKE_CXXFLAGS *= -std=c++11
    DEFINES += QT_DISABLE_DEPRECATED_UP_TO=0x050F00
}

CONFIG += plugin link_pkgconfig
PKGCONFIG += nymea

packagesExist(libopenzwave) {
    PKGCONFIG += libopenzwave
    DEFINES += OZW_16
} else:exists($$[QT_INSTALL_LIBS]/libopenzwave.so) {
    INCLUDEPATH += /usr/include/openzwave/
    LIBS += -lopenzwave
} else {
    erorr("libopenzwave1.6-dev or libopenzwave1.6-dev not found.")
}

SOURCES += \
    openzwavebackend.cpp

HEADERS += \
    openzwavebackend.h

target.path = $$[QT_INSTALL_LIBS]/nymea/zwave/
INSTALLS += target
