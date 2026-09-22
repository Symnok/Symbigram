# The MTProto / Telegram core shared by the phone app (Symbigram.pro) and the desktop
# harness (tools/symbigram-cli). Pure QtCore + QtNetwork, no UI, no third-party code.
# Also generates src/core/tgcredentials.h from credentials.cfg (see credentials.cfg.template).

QT += core network

INCLUDEPATH += $$PWD/src/core

# credentials.cfg: KEY=VALUE lines. Turned into a header so the same values reach both the
# Symbian (sbsv2) and the MinGW builds without fighting two generators' quoting rules.
CREDS_FILE = $$PWD/credentials.cfg
!exists($$CREDS_FILE) {
    error("credentials.cfg is missing - copy credentials.cfg.template to credentials.cfg and fill in the values")
}
GEN_OUT = $$system(python \"$$PWD/tools/gen-credentials.py\")
!exists($$PWD/src/core/tgcredentials.h) {
    error("tools/gen-credentials.py did not produce src/core/tgcredentials.h - is python on the PATH, and is TG_API_ID filled in?")
}

HEADERS += \
    $$PWD/src/core/tlwriter.h \
    $$PWD/src/core/tlreader.h \
    $$PWD/src/core/tlobject.h \
    $$PWD/src/core/tlconstructors.h \
    $$PWD/src/core/bigint.h \
    $$PWD/src/core/crypto.h \
    $$PWD/src/core/inflate.h \
    $$PWD/src/core/telegramservers.h \
    $$PWD/src/core/dh.h \
    $$PWD/src/core/mtprototransport.h \
    $$PWD/src/core/authkeyhandshake.h \
    $$PWD/src/core/mtprotosession.h \
    $$PWD/src/core/mtprotoclient.h \
    $$PWD/src/core/tgtypes.h \
    $$PWD/src/core/tgapi.h \
    $$PWD/src/core/srp.h \
    $$PWD/src/core/qrcode.h \
    $$PWD/src/core/secretchat.h \
    $$PWD/src/core/secretapi.h \
    $$PWD/src/core/telegramsession.h \
    $$PWD/src/core/tgcredentials.h

SOURCES += \
    $$PWD/src/core/tlwriter.cpp \
    $$PWD/src/core/tlreader.cpp \
    $$PWD/src/core/tlobject.cpp \
    $$PWD/src/core/tlschema_data.cpp \
    $$PWD/src/core/secretschema_data.cpp \
    $$PWD/src/core/bigint.cpp \
    $$PWD/src/core/sha2.cpp \
    $$PWD/src/core/aes.cpp \
    $$PWD/src/core/random.cpp \
    $$PWD/src/core/inflate.cpp \
    $$PWD/src/core/telegramservers.cpp \
    $$PWD/src/core/dh.cpp \
    $$PWD/src/core/mtprototransport.cpp \
    $$PWD/src/core/authkeyhandshake.cpp \
    $$PWD/src/core/mtprotosession.cpp \
    $$PWD/src/core/mtprotoclient.cpp \
    $$PWD/src/core/tgapi.cpp \
    $$PWD/src/core/srp.cpp \
    $$PWD/src/core/qrcode.cpp \
    $$PWD/src/core/secretchat.cpp \
    $$PWD/src/core/secretapi.cpp \
    $$PWD/src/core/telegramsession.cpp

win32:LIBS += -ladvapi32
