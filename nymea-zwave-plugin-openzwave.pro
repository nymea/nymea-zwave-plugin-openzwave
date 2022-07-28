QT -= gui

TARGET = nymea_zwavepluginopenzwave
TEMPLATE = lib

CONFIG += plugin link_pkgconfig c++11
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
