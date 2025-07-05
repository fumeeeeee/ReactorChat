QT       += core gui
QT       += network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    log_in.cpp \
    main.cpp \
    client_widget.cpp

HEADERS += \
    client_widget.h \
    log_in.h

FORMS += \
    client_widget.ui \
    log_in.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

INCLUDEPATH += C:/OpenSSL-Win64/include

LIBS += "C:/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib"
LIBS += "C:/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib"

DISTFILES += \
    public.pem


