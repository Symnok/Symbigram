# Symbigram - a Telegram client for Symbian Anna/Belle.
# Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#
# Build with the "Qt 4.7.4 for Symbian Anna/Belle" (SymbianSR1Qt474) kit for the phone
# (build-symbian.cmd), or with the Desktop Qt 4.7.4 MinGW kit to run it on the PC. The
# protocol core (core.pri) is shared with the desktop harness in tools/symbigram-cli.

TEMPLATE = app
TARGET = Symbigram
VERSION = 1.0.32

QT += core gui network declarative

include(core.pri)

INCLUDEPATH += src/app

HEADERS += \
    src/app/appcontroller.h \
    src/app/chatsmodel.h \
    src/app/messagesmodel.h \
    src/app/notifier.h \
    src/app/mediacache.h \
    src/app/qrimageprovider.h \
    src/app/voiceplayer.h \
    src/app/voicerecorder.h \
    src/app/piglernotifier.h

SOURCES += \
    src/main.cpp \
    src/app/appcontroller.cpp \
    src/app/chatsmodel.cpp \
    src/app/messagesmodel.cpp \
    src/app/notifier.cpp \
    src/app/mediacache.cpp \
    src/app/qrimageprovider.cpp \
    src/app/voiceplayer.cpp \
    src/app/voicerecorder.cpp \
    src/app/piglernotifier.cpp

RESOURCES += qml.qrc translations.qrc

# The version reaches the About page and initConnection as an unquoted macro
# (stringified in code), which survives every generator's quoting rules.
DEFINES += APP_VERSION=$$VERSION

TRANSLATIONS += \
    translations/symbigram_ru.ts \
    translations/symbigram_uk.ts \
    translations/symbigram_vi.ts \
    translations/symbigram_he.ts

OTHER_FILES += qml/*.qml README.md credentials.cfg.template

symbian {
    # Unprotected range: installs self-signed without Symbian Signed. Also used by the
    # notifier so that tapping a popup brings this app forward.
    TARGET.UID3 = 0xE4B1C2D3
    DEFINES += SGM_UID3=0xE4B1C2D3
    TARGET.CAPABILITY += NetworkServices ReadUserData WriteUserData UserEnvironment
    # The TL schema table, the DH arithmetic and a few chats' worth of messages: a roomy heap.
    TARGET.EPOCHEAPSIZE = 0x020000 0x4000000
    TARGET.EPOCSTACKSIZE = 0x14000
    ICON = icon.svg

    # Notifier: discreet popups (avkon), the "new messages" global query + status-bar
    # envelope (aknnotify), vibration (hwrm), bringing the app forward (apgrfx, ws32).
    LIBS += -lavkon -laknnotify -lhwrmvibraclient -lcone -leikcore -lapgrfx -lws32
    LIBS += -lmediaclientaudiostream -lmediaclientaudioinputstream -lmediaclientaudio
    INCLUDEPATH += $$[QT_INSTALL_PREFIX]/epoc32/include/platform/mw

    # Pigler Notifications API client (third_party/pigler) - Belle status-bar notifications.
    # Compiled from source (an IPC client to a separately-installed Pigler server); needs only
    # standard libraries, so the app is unaffected when Pigler is not installed.
    INCLUDEPATH += third_party/pigler
    LIBS += -lrandom
    HEADERS += third_party/pigler/QPiglerAPI.h
    SOURCES += \
        third_party/pigler/QPiglerAPI.cpp \
        third_party/pigler/PiglerAPI.cpp \
        third_party/pigler/PiglerTapServer.cpp

    # Qt Quick Components for Symbian (built into Belle; Anna gets them through the Smart
    # Installer package, Symbigram_installer.sis).
    CONFIG += qt-components
    DEPLOYMENT.installer_header = 0x2002CCCF

    vendorinfo = \
        "%{\"Symbigram\"}" \
        ":\"Symbigram\""
    my_deployment.pkg_prerules = vendorinfo
    DEPLOYMENT += my_deployment
}
