// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "appcontroller.h"
#include "chatsmodel.h"
#include "messagesmodel.h"
#include "qrimageprovider.h"
#include "tgtypes.h"
#include "tlobject.h"
#include "authkeyhandshake.h"

#include <QApplication>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeView>
#include <QSettings>
#include <QTranslator>
#include <QUrl>
#include <QtDeclarative>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <cstdio>

// qDebug/qWarning (including QML errors) are kept in a ring the About page shows, and go to
// SGM_LOG_FILE when set - the only way to see them from a GUI-subsystem build.
static QFile *logFile = 0;
static void fileMessageHandler(QtMsgType type, const char *msg)
{
    AppController::appendLog(QString::fromLatin1(type == QtDebugMsg ? "D " : type == QtWarningMsg ? "W " : "E ") + QString::fromLocal8Bit(msg));
    if (!logFile) return;
    QTextStream out(logFile);
    out << QDateTime::currentDateTime().toString(QLatin1String("HH:mm:ss.zzz ")) << (type == QtDebugMsg ? "D " : type == QtWarningMsg ? "W " : "E ") << msg << endl;
    out.flush();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QByteArray logPath = qgetenv("SGM_LOG_FILE");
    if (!logPath.isEmpty()) {
        logFile = new QFile(QString::fromLocal8Bit(logPath));
        if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) { delete logFile; logFile = 0; }
    }
    qInstallMsgHandler(fileMessageHandler);
    app.setApplicationName(QLatin1String("Symbigram"));
    app.setOrganizationName(QLatin1String("Symbigram"));
    app.setQuitOnLastWindowClosed(true);

    qRegisterMetaType<TlObject>("TlObject");
    qRegisterMetaType<TgPeer>("TgPeer");
    qRegisterMetaType<TgMessage>("TgMessage");
    qRegisterMetaType<QList<TgMessage> >("QList<TgMessage>");
    qRegisterMetaType<AuthKey>("AuthKey");

    QSettings settings(QLatin1String("Symbigram"), QLatin1String("Symbigram"));
    const QString lang = AppController::effectiveLanguage(settings);
    QTranslator translator;
    if (lang != QLatin1String("en") && translator.load(QLatin1String(":/translations/symbigram_") + lang))
        app.installTranslator(&translator);

    // Hebrew is right-to-left: flip the whole UI. The Symbian components and the default text
    // alignment follow the application layout direction; main.qml adds LayoutMirroring so the
    // custom anchor-based layouts mirror too.
    if (lang == QLatin1String("he"))
        app.setLayoutDirection(Qt::RightToLeft);

    qmlRegisterType<ChatsModel>();
    qmlRegisterType<MessagesModel>();

    AppController controller;

    QDeclarativeView view;
    view.setResizeMode(QDeclarativeView::SizeRootObjectToView);
    view.engine()->addImageProvider(QLatin1String("qr"), new QrImageProvider);
    view.rootContext()->setContextProperty(QLatin1String("app"), &controller);
    view.rootContext()->setContextProperty(QLatin1String("uiLanguage"), lang);
    controller.setView(&view);
    view.setSource(QUrl(QLatin1String("qrc:/qml/main.qml")));
    if (view.status() == QDeclarativeView::Error) {
        QList<QDeclarativeError> errors = view.errors();
        for (int i = 0; i < errors.size(); ++i) qWarning() << "QML:" << errors.at(i).toString();
    }

#if defined(Q_OS_SYMBIAN) || defined(Q_WS_SIMULATOR)
    view.setAttribute(Qt::WA_LockPortraitOrientation, true);
    view.showFullScreen();
#else
    view.resize(360, 640);
    view.show();
#endif

    controller.start();
    return app.exec();
}
