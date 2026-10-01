// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "appcontroller.h"
#include "chatsmodel.h"
#include "messagesmodel.h"
#include "topicsmodel.h"
#include "mediacache.h"
#include "voicerecorder.h"
#include "notifier.h"
#include "piglernotifier.h"
#include "telegramsession.h"
#include "tgapi.h"
#include "tgcredentials.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDeclarativeView>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QLocale>
#include <QNetworkConfigurationManager>
#include <QNetworkSession>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QDebug>

#ifndef APP_VERSION
#define APP_VERSION 0.0.0
#endif
#define SGM_STR2(x) #x
#define SGM_STR(x) SGM_STR2(x)

namespace
{
    const char *const KeyAutoConnect = "account/autoConnect";
    const char *const KeyLanguage = "ui/language";
    // Three alert channels, each 0 off / 1 first message only / 2 every message.
    const char *const KeyPopupMode = "ui/popupMode";
    const char *const KeySoundMode = "ui/soundMode";
    const char *const KeyVibrationMode = "ui/vibrationMode";
    const char *const KeySoundVolume = "ui/soundVolume";
    const char *const KeyVibrationLength = "ui/vibrationLength";
    const char *const KeyGroupNotifications = "ui/groupNotifications";
    const char *const KeyImagePreview = "ui/imagePreview";
    const char *const KeyFontSize = "ui/fontSize";
    const char *const KeyLogging = "ui/logging";
    const char *const KeyDownloadDrive = "downloads/drive";
    const char *const KeyDownloadCustom = "downloads/folder";
    const char *const KeyProxyEnabled = "proxy/enabled";
    const char *const KeyProxyHost = "proxy/host";
    const char *const KeyProxyPort = "proxy/port";
    const char *const KeyProxyUser = "proxy/user";
    const char *const KeyProxyPass = "proxy/pass";
    const int ReconnectMinMs = 5000;
    const int ReconnectMaxMs = 60000;

    QStringList &logLines() { static QStringList lines; return lines; }
    AppController *logOwner = 0;
    // Whether the About-page log ring is collected at all (a setting; see setLogging).
    bool loggingEnabled = false;
}

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_settings(QLatin1String("Symbigram"), QLatin1String("Symbigram")),
      m_netMgr(0), m_netSession(0), m_view(0),
      m_state(QLatin1String("starting")), m_busy(false), m_wantOnline(false), m_everOnline(false),
      m_reconnectDelay(ReconnectMinMs), m_checkingPassword(false), m_loginMethod(QLatin1String("qr")), m_codeBusy(false), m_foreground(true)
{
    m_session = new TelegramSession(this);
    ClientInfo info;
    info.apiId = TG_API_ID;
    info.apiHash = QLatin1String(TG_API_HASH);
#ifdef Q_OS_SYMBIAN
    info.deviceModel = QLatin1String("Nokia Symbian (Symbigram)");
    info.systemVersion = QLatin1String("Symbian Belle");
#else
    info.deviceModel = QLatin1String("PC (Symbigram desktop)");
    info.systemVersion = QLatin1String("Windows");
#endif
    info.appVersion = QLatin1String("Symbigram ") + QLatin1String(SGM_STR(APP_VERSION));
    info.systemLangCode = QLocale::system().name().left(2).toLower();
    info.langCode = effectiveLanguage(m_settings);
    m_session->setClientInfo(info);
    m_session->setSessionFile(dataDir() + QLatin1String("/session.dat"));

    m_media = new MediaCache(m_session, this);
    m_media->setDirectory(dataDir() + QLatin1String("/media"));
    m_chats = new ChatsModel(m_session, m_media, this);
    m_chat = new MessagesModel(m_session, m_media, this);
    m_chat->setAutoPreview(imagePreview() == 1);   // Image Preview: Full auto-loads, Thumbnail waits for a tap
    m_topics = new TopicsModel(m_session, this);
    applyDownloadFolder();
    m_session->setProxy(proxyEnabled(), proxyHost(), proxyPort().toInt(), proxyUser(), proxyPass());

    m_recorder = new VoiceRecorder(this);
    connect(m_recorder, SIGNAL(started()), this, SIGNAL(recordingChanged()));
    connect(m_recorder, SIGNAL(recorded(QString,int,QByteArray)), this, SLOT(onRecorded(QString,int,QByteArray)));
    connect(m_recorder, SIGNAL(failed(QString)), this, SLOT(onRecordFailed(QString)));
    m_notifier = new Notifier(this);
    m_notifier->setSoundVolume(soundVolume());
    m_notifier->setVibrationMs(vibrationMsFor(vibrationLength()));

    // Optional status-bar ("envelope") notifications on Belle via Pigler. init() quietly fails
    // (and everything stays as-is) when Pigler is not installed, or on S^3/Anna.
    m_pigler = new PiglerNotifier(this);
#ifdef SGM_UID3
    m_pigler->init(QLatin1String("Symbigram"), SGM_UID3);
#else
    m_pigler->init(QLatin1String("Symbigram"), 0);
#endif
    connect(m_pigler, SIGNAL(tapped(QString)), this, SIGNAL(openChatRequested(QString)));

    connect(m_session, SIGNAL(stateChanged()), this, SLOT(onSessionState()));
    connect(m_session, SIGNAL(disconnected(QString)), this, SLOT(onSessionDisconnected(QString)));
    connect(m_session, SIGNAL(signedIn()), this, SLOT(onSignedIn()));
    connect(m_session, SIGNAL(signedOut(QString)), this, SLOT(onSignedOut(QString)));
    connect(m_session, SIGNAL(loginError(QString)), this, SLOT(onLoginError(QString)));
    connect(m_session, SIGNAL(qrChanged()), this, SLOT(onLoginChanged()));
    connect(m_session, SIGNAL(passwordNeededChanged()), this, SLOT(onLoginChanged()));
    connect(m_session, SIGNAL(codeNeededChanged()), this, SLOT(onLoginChanged()));
    connect(m_session, SIGNAL(selfChanged()), this, SIGNAL(selfChanged()));
    connect(m_session, SIGNAL(messageReceived(TgMessage)), this, SLOT(onMessage(TgMessage)));
    connect(m_session, SIGNAL(peerResolved(TgPeer)), this, SLOT(onPeerResolved(TgPeer)));
    connect(m_session, SIGNAL(peersFound(QString,QList<TgPeer>)), this, SLOT(onPeersFound(QString,QList<TgPeer>)));
    connect(m_session, SIGNAL(secretChatRequested(int,qint64)), this, SLOT(onSecretRequested(int,qint64)));
    connect(m_session, SIGNAL(secretChatReady(int)), this, SLOT(onSecretReady(int)));
    connect(m_session, SIGNAL(secretMessageReceived(int,qint64,QString,int,bool,int)), this, SLOT(onSecretMessage(int,qint64,QString,int,bool,int)));
    connect(m_session, SIGNAL(resolveFailed(QString)), this, SLOT(onResolveFailed(QString)));
    connect(m_session, SIGNAL(notice(QString)), this, SLOT(onNotice(QString)));
    connect(m_session, SIGNAL(log(QString)), this, SLOT(onSessionLog(QString)));
    connect(m_chat, SIGNAL(sendFailed(QString)), this, SLOT(onNotice(QString)));

    m_reconnect = new QTimer(this);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, SIGNAL(timeout()), this, SLOT(onReconnectTimer()));

    qApp->installEventFilter(this);
    logOwner = this;
    loggingEnabled = logging();
}

AppController::~AppController()
{
    m_session->disconnectFromServer();
    if (m_netSession) m_netSession->close();
}

QString AppController::dataDir()
{
    QString base = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#ifndef Q_OS_SYMBIAN
    // Desktop testing: a separate data folder per scenario (e.g. an empty one for the
    // login page).
    if (!qgetenv("SGM_DATA_DIR").isEmpty()) base = QString::fromLocal8Bit(qgetenv("SGM_DATA_DIR"));
#endif
    if (base.isEmpty()) base = QDir::homePath() + QLatin1String("/.symbigram");
    QDir().mkpath(base);
    return base;
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ApplicationActivate) {
        m_foreground = true;
        if (m_notifier->pendingCount() > 0) m_notifier->setPendingCount(0);
        m_pigler->clearAll();          // coming to the foreground clears the status-bar entries
        m_session->setOnline(true);
    } else if (event->type() == QEvent::ApplicationDeactivate) {
        m_foreground = false;
        m_session->setOnline(false);
    }
    return QObject::eventFilter(watched, event);
}

// -- properties ----------------------------------------------------------------------------------

void AppController::setState(const QString &s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

void AppController::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

void AppController::setNotice(const QString &n)
{
    m_notice = n;
    emit noticeChanged();
}

void AppController::clearNotice() { setNotice(QString()); }
void AppController::onNotice(const QString &text) { setNotice(text); }

QString AppController::cacheSize() const
{
    qint64 b = m_media ? m_media->cacheBytes() : 0;
    if (b >= 1048576) return tr("%1 MB").arg(double(b) / 1048576.0, 0, 'f', 1);
    if (b >= 1024) return tr("%1 KB").arg(int(b / 1024));
    return tr("%1 B").arg(b);
}

void AppController::clearCache()
{
    if (!m_media) return;
    qint64 freed = m_media->clearCache();
    QString human = freed >= 1048576 ? tr("%1 MB").arg(double(freed) / 1048576.0, 0, 'f', 1)
                  : (freed >= 1024 ? tr("%1 KB").arg(int(freed / 1024)) : tr("%1 B").arg(freed));
    setNotice(tr("Cache cleared (%1 freed).").arg(human));
    emit cacheChanged();
}

QString AppController::connection() const
{
    switch (m_session->state()) {
    case TelegramSession::Disconnected: return QLatin1String("offline");
    case TelegramSession::Online: return QLatin1String("online");
    default: return QLatin1String("connecting");
    }
}

QString AppController::loginStatus() const
{
    if (m_state != QLatin1String("login")) return QString();
    switch (m_session->state()) {
    case TelegramSession::Disconnected: return tr("Not connected.");
    case TelegramSession::Connecting: return m_session->isSignedIn() ? tr("Connecting...") : tr("Connecting and creating the encryption key...");
    case TelegramSession::LoggingIn:
        if (m_checkingPassword) return tr("Checking the password (this takes a few seconds)...");
        if (m_session->passwordNeeded()) return tr("This account has two-step verification. Enter the password.");
        if (m_loginMethod == QLatin1String("phone")) {
            if (m_session->codeNeeded()) return m_codeBusy ? tr("Signing in...") : tr("Enter the code Telegram sent to %1.").arg(loginPhone());
            return m_codeBusy ? tr("Requesting a code...") : tr("Enter your phone number, with the country code, to get a code.");
        }
        return m_session->qrUrl().isEmpty() ? tr("Requesting a sign-in code...") : tr("Waiting for the code to be scanned...");
    default: return tr("Signing in...");
    }
}

bool AppController::codeNeeded() const { return m_session->codeNeeded(); }

QString AppController::loginPhone() const
{
    QString digits = m_session->loginPhone();
    return digits.isEmpty() ? QString() : QLatin1Char('+') + digits;
}

QString AppController::qrToken() const
{
    QString url = m_session->qrUrl();
    const QString prefix = QLatin1String("tg://login?token=");
    return url.startsWith(prefix) ? url.mid(prefix.size()) : QString();
}

int AppController::qrExpires() const { return m_session->qrExpires(); }
bool AppController::passwordNeeded() const { return m_session->passwordNeeded(); }
QString AppController::passwordHint() const { return m_session->passwordHint(); }
QString AppController::version() const { return QLatin1String(SGM_STR(APP_VERSION)); }
static int clampMode(int m) { return (m < 0 || m > 2) ? 1 : m; }

int AppController::soundMode() const { return clampMode(m_settings.value(QLatin1String(KeySoundMode), 1).toInt()); }
void AppController::setSoundMode(int mode)
{
    if (mode < 0 || mode > 2 || mode == soundMode()) return;
    m_settings.setValue(QLatin1String(KeySoundMode), mode);
    emit settingsChanged();
}
int AppController::vibrationMode() const { return clampMode(m_settings.value(QLatin1String(KeyVibrationMode), 1).toInt()); }
void AppController::setVibrationMode(int mode)
{
    if (mode < 0 || mode > 2 || mode == vibrationMode()) return;
    m_settings.setValue(QLatin1String(KeyVibrationMode), mode);
    emit settingsChanged();
}
int AppController::popupMode() const
{
    int m = m_settings.value(QLatin1String(KeyPopupMode), 1).toInt();   // default: first message
    return (m < 0 || m > 2) ? 1 : m;
}
void AppController::setPopupMode(int mode)
{
    if (mode < 0 || mode > 2 || mode == popupMode()) return;
    m_settings.setValue(QLatin1String(KeyPopupMode), mode);
    emit settingsChanged();
}
int AppController::soundVolume() const
{
    int v = m_settings.value(QLatin1String(KeySoundVolume), 0).toInt();
    return (v < 0 || v > 2) ? 0 : v;
}

void AppController::setSoundVolume(int level)
{
    if (level < 0 || level > 2 || level == soundVolume()) return;
    m_settings.setValue(QLatin1String(KeySoundVolume), level);
    if (m_notifier) m_notifier->setSoundVolume(level);
    emit settingsChanged();
}

QStringList AppController::soundVolumeNames() const
{
    return QStringList() << tr("Low") << tr("Medium") << tr("Loud");
}

int AppController::vibrationLength() const
{
    int v = m_settings.value(QLatin1String(KeyVibrationLength), 0).toInt();
    return (v < 0 || v > 4) ? 0 : v;
}

void AppController::setVibrationLength(int index)
{
    if (index < 0 || index > 4 || index == vibrationLength()) return;
    m_settings.setValue(QLatin1String(KeyVibrationLength), index);
    if (m_notifier) m_notifier->setVibrationMs(vibrationMsFor(index));
    emit settingsChanged();
}

QStringList AppController::vibrationLengthNames() const
{
    return QStringList() << tr("Short (0.4 s)") << tr("1 second") << tr("2 seconds")
                         << tr("3 seconds") << tr("5 seconds");
}

int AppController::vibrationMsFor(int index)
{
    static const int ms[5] = { 400, 1000, 2000, 3000, 5000 };
    return ms[(index < 0 || index > 4) ? 0 : index];
}

QStringList AppController::popupModeNames() const
{
    QStringList names;
    names << tr("Off") << tr("First message only") << tr("Every message");
    return names;
}
bool AppController::groupNotifications() const { return m_settings.value(QLatin1String(KeyGroupNotifications), true).toBool(); }
void AppController::setGroupNotifications(bool on) { m_settings.setValue(QLatin1String(KeyGroupNotifications), on); emit settingsChanged(); }

// Image Preview: 0 = Thumbnail (show the blurred placeholder, load a photo on tap), 1 = Full
// (fetch and show photo previews automatically). Does not change what Save/Open download.
int AppController::imagePreview() const { return m_settings.value(QLatin1String(KeyImagePreview), 0).toInt() == 1 ? 1 : 0; }
void AppController::setImagePreview(int mode)
{
    if (mode == imagePreview()) return;
    m_settings.setValue(QLatin1String(KeyImagePreview), mode == 1 ? 1 : 0);
    if (m_chat) m_chat->setAutoPreview(mode == 1);
    emit settingsChanged();
}
int AppController::fontSize() const
{
    // Small (1) is the original size; Medium (2) is the default, a notch larger.
    int v = m_settings.value(QLatin1String(KeyFontSize), 2).toInt();
    return (v < 0 || v > 4) ? 2 : v;
}

void AppController::setFontSize(int index)
{
    if (index < 0 || index > 4 || index == fontSize()) return;
    m_settings.setValue(QLatin1String(KeyFontSize), index);
    emit settingsChanged();
}

QStringList AppController::fontSizeNames() const
{
    return QStringList() << tr("Extra small") << tr("Small") << tr("Medium")
                         << tr("Large") << tr("Extra large");
}

qreal AppController::fontScale() const
{
    static const qreal scale[5] = { 0.8, 1.0, 1.15, 1.3, 1.5 };
    return scale[fontSize()];
}

QStringList AppController::imagePreviewNames() const { return QStringList() << tr("Thumbnail") << tr("Full"); }
bool AppController::autoConnect() const { return m_settings.value(QLatin1String(KeyAutoConnect), true).toBool(); }
void AppController::setAutoConnect(bool on) { m_settings.setValue(QLatin1String(KeyAutoConnect), on); emit settingsChanged(); }
bool AppController::logging() const { return m_settings.value(QLatin1String(KeyLogging), false).toBool(); }
void AppController::setLogging(bool on)
{
    m_settings.setValue(QLatin1String(KeyLogging), on);
    loggingEnabled = on;
    if (!on) { logLines().clear(); emit logChanged(); }   // stop collecting and empty the ring
    emit settingsChanged();
}

// -- download folder (Settings: which drive receives saved files) ----------------------------------

QStringList AppController::presentDriveLetters()
{
    QStringList out;
    const char *const cands[] = { "C:", "E:", "F:" };
    for (int i = 0; i < 3; ++i) {
        QString d = QLatin1String(cands[i]);
        if (QDir(d + QLatin1String("/")).exists()) out << d;
    }
    if (out.isEmpty()) out << QLatin1String("C:");
    return out;
}

QString AppController::folderForDrive(const QString &drive)
{
    // C: keeps files in the user area (C:\Data); a card/mass drive uses its root.
    if (drive == QLatin1String("C:")) return QLatin1String("C:/Data/Symbigram");
    return drive + QLatin1String("/Symbigram");
}

QStringList AppController::downloadDrives() const
{
    QStringList letters = presentDriveLetters();
    QStringList labels;
    for (int i = 0; i < letters.size(); ++i) {
        QString d = letters.at(i);
        QString name = d == QLatin1String("C:") ? tr("Phone memory")
                     : d == QLatin1String("E:") ? tr("Mass memory")
                     : tr("Memory card");
        labels << QString::fromLatin1("%1 (%2)").arg(name, d);
    }
    return labels;
}

int AppController::downloadDriveIndex() const
{
    QStringList letters = presentDriveLetters();
    QString sel = m_settings.value(QLatin1String(KeyDownloadDrive)).toString();
    int i = letters.indexOf(sel);
    if (i >= 0) return i;
    // Default: prefer a card / mass memory (last present non-C:), else phone memory.
    for (int j = letters.size() - 1; j >= 0; --j) if (letters.at(j) != QLatin1String("C:")) return j;
    return 0;
}

void AppController::setDownloadDriveIndex(int index)
{
    QStringList letters = presentDriveLetters();
    if (index < 0 || index >= letters.size()) return;
    m_settings.setValue(QLatin1String(KeyDownloadDrive), letters.at(index));
    m_settings.remove(QLatin1String(KeyDownloadCustom));   // a drive choice overrides a picked folder
    applyDownloadFolder();
    emit settingsChanged();
}

QString AppController::downloadFolder() const
{
    QString custom = m_settings.value(QLatin1String(KeyDownloadCustom)).toString();
    if (!custom.isEmpty()) return QDir::toNativeSeparators(custom);
    QStringList letters = presentDriveLetters();
    int i = downloadDriveIndex();
    return QDir::toNativeSeparators(folderForDrive(letters.at(i)));
}

bool AppController::downloadCustom() const
{
    return !m_settings.value(QLatin1String(KeyDownloadCustom)).toString().isEmpty();
}

void AppController::chooseDownloadFolder()
{
    QString start = m_settings.value(QLatin1String(KeyDownloadCustom)).toString();
    if (start.isEmpty()) { QStringList l = presentDriveLetters(); start = folderForDrive(l.at(downloadDriveIndex())); }
    QDir().mkpath(start);
    QString dir = QFileDialog::getExistingDirectory(0, tr("Choose download folder"), start);
    if (dir.isEmpty()) return;                       // cancelled
    m_settings.setValue(QLatin1String(KeyDownloadCustom), QDir::fromNativeSeparators(dir));
    applyDownloadFolder();
    emit settingsChanged();
}

void AppController::applyDownloadFolder()
{
    // Make sure a Symbigram folder exists on every present drive, and point the chat model
    // at the chosen one.
    QStringList letters = presentDriveLetters();
    for (int i = 0; i < letters.size(); ++i) QDir().mkpath(folderForDrive(letters.at(i)));
    QString custom = m_settings.value(QLatin1String(KeyDownloadCustom)).toString();
    QString chosen = custom.isEmpty() ? folderForDrive(letters.at(downloadDriveIndex())) : custom;
    QDir().mkpath(chosen);
    m_downloadPath = chosen;
    if (m_chat) m_chat->setDownloadFolder(chosen);
}

// -- voice recording -------------------------------------------------------------------------------

bool AppController::recording() const { return m_recorder && m_recorder->recording(); }
int AppController::recordingMs() const { return m_recorder ? m_recorder->elapsedMs() : 0; }

void AppController::startRecording()
{
    if (!m_session->isOnline()) { setNotice(tr("Not connected.")); return; }
    if (m_chat->peerKey().isEmpty() || m_chat->peerIsChannel() || m_chat->isSecret()) {
        setNotice(tr("Cannot record a voice message here."));
        return;
    }
    m_recorder->start(m_downloadPath);
    emit recordingChanged();
}

void AppController::stopRecording()
{
    m_recorder->stop();
    emit recordingChanged();
}

void AppController::cancelRecording()
{
    m_recorder->cancel();
    emit recordingChanged();
}

void AppController::onRecorded(const QString &oggPath, int durationSec, const QByteArray &waveform)
{
    if (m_chat->peerKey().isEmpty()) { emit recordingChanged(); return; }
    m_session->sendVoice(m_chat->peer(), oggPath, durationSec, waveform);
    setNotice(tr("Sending the voice message..."));
    emit recordingChanged();
}

void AppController::onRecordFailed(const QString &error)
{
    setNotice(error);
    emit recordingChanged();
}

// -- SOCKS5 proxy ----------------------------------------------------------------------------------

bool AppController::proxyEnabled() const { return m_settings.value(QLatin1String(KeyProxyEnabled), false).toBool(); }
QString AppController::proxyHost() const { return m_settings.value(QLatin1String(KeyProxyHost)).toString(); }
QString AppController::proxyPort() const { return m_settings.value(QLatin1String(KeyProxyPort)).toString(); }
QString AppController::proxyUser() const { return m_settings.value(QLatin1String(KeyProxyUser)).toString(); }
QString AppController::proxyPass() const { return m_settings.value(QLatin1String(KeyProxyPass)).toString(); }

void AppController::saveProxy(bool enabled, const QString &host, const QString &port, const QString &user, const QString &pass)
{
    m_settings.setValue(QLatin1String(KeyProxyEnabled), enabled);
    m_settings.setValue(QLatin1String(KeyProxyHost), host.trimmed());
    m_settings.setValue(QLatin1String(KeyProxyPort), port.trimmed());
    m_settings.setValue(QLatin1String(KeyProxyUser), user);
    m_settings.setValue(QLatin1String(KeyProxyPass), pass);
    applyProxy();
    emit settingsChanged();
}

void AppController::setProxyEnabled(bool on)
{
    m_settings.setValue(QLatin1String(KeyProxyEnabled), on);
    applyProxy();
    emit settingsChanged();
}

void AppController::applyProxy()
{
    m_session->setProxy(proxyEnabled(), proxyHost(), proxyPort().toInt(), proxyUser(), proxyPass());

    m_recorder = new VoiceRecorder(this);
    connect(m_recorder, SIGNAL(started()), this, SIGNAL(recordingChanged()));
    connect(m_recorder, SIGNAL(recorded(QString,int,QByteArray)), this, SLOT(onRecorded(QString,int,QByteArray)));
    connect(m_recorder, SIGNAL(failed(QString)), this, SLOT(onRecordFailed(QString)));
    // Reconnect so the change takes effect on a live connection.
    bool wasConnected = m_session->state() != TelegramSession::Disconnected;
    if (wasConnected) m_session->disconnectFromServer();
    if (m_wantOnline) { m_reconnect->stop(); m_reconnectDelay = ReconnectMinMs; connectSession(); }
}
QString AppController::myName() const { return m_session->selfName(); }

QString AppController::mySubtitle() const
{
    TgPeerInfo me = m_session->selfInfo();
    if (!me.username.isEmpty()) return QLatin1Char('@') + me.username;
    if (!me.phone.isEmpty()) return QLatin1Char('+') + me.phone;
    return QString();
}

QString AppController::language() const
{
    return effectiveLanguage(m_settings);
}

void AppController::setLanguage(const QString &lang)
{
    m_settings.setValue(QLatin1String(KeyLanguage), lang);
    emit settingsChanged();
}

QString AppController::effectiveLanguage(const QSettings &settings)
{
    const QString chosen = settings.value(QLatin1String(KeyLanguage)).toString();
    if (!chosen.isEmpty()) return chosen;
    const QString sys = QLocale::system().name().left(2).toLower();
    return (sys == QLatin1String("ru") || sys == QLatin1String("uk")) ? sys : QString::fromLatin1("en");
}

// -- start / network ---------------------------------------------------------------------------------

void AppController::start()
{
    setState(QLatin1String("starting"));
    setBusy(true);
    // On the phone a socket without an open QNetworkSession either fails or prompts for an
    // access point every time. Open the default configuration once, up front.
    m_netMgr = new QNetworkConfigurationManager(this);
    const QNetworkConfiguration cfg = m_netMgr->defaultConfiguration();
    if (!cfg.isValid() || !(m_netMgr->capabilities() & QNetworkConfigurationManager::NetworkSessionRequired)) {
        continueStart();
        return;
    }
    m_netSession = new QNetworkSession(cfg, this);
    connect(m_netSession, SIGNAL(opened()), this, SLOT(onNetworkOpened()));
    connect(m_netSession, SIGNAL(error(QNetworkSession::SessionError)), this, SLOT(onNetworkError()));
    m_netSession->open();
}

void AppController::onNetworkOpened() { continueStart(); }

void AppController::onNetworkError()
{
    qWarning() << "network session error:" << (m_netSession ? m_netSession->errorString() : QString());
    continueStart();
}

void AppController::continueStart()
{
    static bool started = false;
    if (started) return;
    started = true;

    if (m_session->isSignedIn()) {
        setState(QLatin1String("ready"));
        if (autoConnect()) {
            m_wantOnline = true;
            connectSession();
        } else {
            setBusy(false);
        }
        return;
    }
    // No account: the QR page, connecting right away so the code is ready to scan.
    setState(QLatin1String("login"));
    m_wantOnline = true;
    connectSession();
}

void AppController::connectSession()
{
    setBusy(true);
    m_session->connectToServer();
    emit loginChanged();
}

// -- session events -------------------------------------------------------------------------------

void AppController::onSessionState()
{
    emit connectionChanged();
    emit loginChanged();
    if (m_session->state() == TelegramSession::Online) {
        setBusy(false);
        m_everOnline = true;
        m_reconnectDelay = ReconnectMinMs;
        m_session->setOnline(m_foreground);
    }
    if (m_session->state() == TelegramSession::LoggingIn) setBusy(false);
}

void AppController::onSessionDisconnected(const QString &reason)
{
    setBusy(false);
    m_checkingPassword = false;
    emit loginChanged();
    if (m_wantOnline && !reason.isEmpty()) {
        if (m_state == QLatin1String("ready")) setNotice(tr("Connection lost: %1. Reconnecting...").arg(reason));
        else m_loginError = tr("Connection failed: %1. Retrying...").arg(reason);
        emit loginChanged();
        m_reconnect->start(m_reconnectDelay);
        m_reconnectDelay = qMin(m_reconnectDelay * 2, ReconnectMaxMs);
    }
}

void AppController::onReconnectTimer()
{
    if (m_wantOnline && m_session->state() == TelegramSession::Disconnected) connectSession();
}

void AppController::onSignedIn()
{
    m_loginError.clear();
    m_checkingPassword = false;
    emit loginChanged();
    setState(QLatin1String("ready"));
}

void AppController::onSignedOut(const QString &reason)
{
    m_chat->close();
    m_pigler->clearAll();
    setBusy(false);
    m_checkingPassword = false;
    m_loginError = reason;
    emit loginChanged();
    setState(QLatin1String("login"));
    // Straight back to a fresh QR code.
    m_wantOnline = true;
    m_reconnectDelay = ReconnectMinMs;
    m_reconnect->start(500);
}

void AppController::onLoginError(const QString &error)
{
    m_loginError = error;
    m_checkingPassword = false;
    m_codeBusy = false;
    emit loginChanged();
}

void AppController::onLoginChanged()
{
    if (m_session->passwordNeeded() == false) m_checkingPassword = false;
    // The code arrived (or the password step took over): the request is no longer in flight.
    if (m_session->codeNeeded() || m_session->passwordNeeded()) m_codeBusy = false;
    emit loginChanged();
}

void AppController::onSessionLog(const QString &line)
{
    if (loggingEnabled) qDebug() << "tg:" << line;
}

bool AppController::appInForeground() const
{
    return m_foreground && m_view && m_view->isActiveWindow();
}

void AppController::onMessage(const TgMessage &m)
{
    if (m.out || m.service) return;
    if (m_session->selfId() && m.fromId == m_session->selfId()) return;
    const bool foreground = appInForeground();
    bool chatOpen = m_chat->peer() == m.peer && foreground;
    if (chatOpen) {
        m_chat->markRead();
        return;
    }
    if (foreground) return;
    if (!notifications()) return;                 // the master switch silences everything, Pigler too

    QString who = m_session->peers().title(m.peer);
    if (m.peer.isGroup()) who = m_session->peers().userName(m.fromId) + QLatin1String(" @ ") + who;
    QString text = m.text.isEmpty() ? m.note : m.text;

    // Pigler (Belle notification-drawer entry) fires for incoming messages while backgrounded. It
    // deliberately ignores the per-chat Telegram mute and the Archive, but DOES respect the
    // groups/channels toggle (user request): a silenced group posts nothing to Pigler either. No-op
    // when Pigler isn't installed. (Cleared when the app comes to the foreground.)
    const bool groupSilenced = m.peer.isGroup() && !groupNotifications() && !m.mentioned;
    if (!groupSilenced)
        m_pigler->showMessage(m.peer.key(), who, text);

    // The pop-up / "new messages" query / vibration still respect the fine-grained silencers: a
    // per-chat mute (from Telegram's notify settings), the Archive, and the groups/channels toggle.
    if (m_session->isArchived(m.peer)) return;
    TgDialog d = m_session->dialog(m.peer);
    if (d.isMuted(int(QDateTime::currentDateTime().toTime_t()))) return;
    if (groupSilenced) return;
    // Each channel: 0 = never, 1 = only the first unread of a burst (count was 0), 2 = every message.
    const bool first = m_notifier->pendingCount() == 0;
    const bool showPopup = popupMode() == 2 || (popupMode() == 1 && first);
    const bool playSound = soundMode() == 2 || (soundMode() == 1 && first);
    const bool vibrate = vibrationMode() == 2 || (vibrationMode() == 1 && first);
    m_notifier->alert(who, text, showPopup, playSound, vibrate);
    m_notifier->setPendingCount(m_notifier->pendingCount() + 1, showPopup);
}

// -- actions ---------------------------------------------------------------------------------------------

void AppController::checkPassword(const QString &password)
{
    if (password.isEmpty()) { m_loginError = tr("Enter the password."); emit loginChanged(); return; }
    m_loginError.clear();
    m_checkingPassword = true;
    emit loginChanged();
    m_session->checkPassword(password);
}

void AppController::usePhoneLogin()
{
    m_loginMethod = QLatin1String("phone");
    m_loginError.clear();
    emit loginChanged();
}

void AppController::useQrLogin()
{
    m_loginMethod = QLatin1String("qr");
    m_loginError.clear();
    m_codeBusy = false;
    m_session->cancelPhoneLogin();          // stop the phone flow, resume the QR code
    emit loginChanged();
}

void AppController::sendLoginCode(const QString &phone)
{
    if (phone.trimmed().isEmpty()) { m_loginError = tr("Enter your phone number."); emit loginChanged(); return; }
    m_loginMethod = QLatin1String("phone");
    m_loginError.clear();
    m_codeBusy = true;
    emit loginChanged();
    m_session->startPhoneLogin(phone);
}

void AppController::submitLoginCode(const QString &code)
{
    if (code.trimmed().isEmpty()) { m_loginError = tr("Enter the code."); emit loginChanged(); return; }
    m_loginError.clear();
    m_codeBusy = true;
    emit loginChanged();
    m_session->submitCode(code);
}

void AppController::resendLoginCode()
{
    m_loginError.clear();
    m_codeBusy = true;
    emit loginChanged();
    m_session->resendCode();
}

void AppController::changeLoginNumber()
{
    m_loginMethod = QLatin1String("phone");
    m_loginError.clear();
    m_codeBusy = false;
    m_session->backToPhoneEntry();
    emit loginChanged();
}

void AppController::signOut()
{
    m_wantOnline = false;
    m_reconnect->stop();
    m_chat->close();
    setBusy(true);
    m_session->logOut();
}

void AppController::goOffline()
{
    m_wantOnline = false;
    m_reconnect->stop();
    m_session->disconnectFromServer();
    setBusy(false);
    emit connectionChanged();
}

void AppController::reconnect()
{
    m_wantOnline = true;
    m_reconnect->stop();
    m_reconnectDelay = ReconnectMinMs;
    if (m_session->state() == TelegramSession::Disconnected) connectSession();
}

void AppController::findPeer(const QString &query)
{
    if (!m_session->isOnline()) { setNotice(tr("Not connected.")); return; }
    m_session->resolve(query);
}

bool AppController::openInternalLink(const QString &url)
{
    QString rest = url.trimmed();
    if (rest.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) rest = rest.mid(8);
    else if (rest.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) rest = rest.mid(7);
    if (rest.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) rest = rest.mid(4);

    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash < 0) return false;
    const QString host = rest.left(slash).toLower();
    if (host != QLatin1String("t.me") && host != QLatin1String("telegram.me")
        && host != QLatin1String("telegram.dog")) return false;

    // The first path segment is the username; anything after it (a message id) is ignored.
    QString path = rest.mid(slash + 1).section(QLatin1Char('?'), 0, 0).section(QLatin1Char('#'), 0, 0);
    const QString name = path.section(QLatin1Char('/'), 0, 0);
    if (name.isEmpty()) return false;

    // Private invites (t.me/+hash, /joinchat/) and the special links are not usernames and we
    // cannot resolve them - let the browser deal with those.
    if (name.startsWith(QLatin1Char('+'))) return false;
    static const char *const reserved[] = {
        "joinchat", "s", "c", "proxy", "socks", "addstickers", "addtheme", "addemoji",
        "addlist", "share", "iv", "setlanguage", "confirmphone", "login", "bg",
        "invoice", "giftcode", "contact", "boost", 0
    };
    for (int i = 0; reserved[i]; ++i)
        if (name.compare(QLatin1String(reserved[i]), Qt::CaseInsensitive) == 0) return false;
    for (int i = 0; i < name.size(); ++i) {
        const QChar ch = name.at(i);
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_')) return false;
    }

    if (!m_session->isOnline()) { setNotice(tr("Not connected.")); return true; }
    setNotice(tr("Opening @%1...").arg(name));
    m_session->resolve(name);   // peerResolved -> peerFound -> the chat (or its topics) opens
    return true;
}

void AppController::onPeerResolved(const TgPeer &peer)
{
    emit peerFound(peer.key());
}

bool AppController::isForum(const QString &peerKey) const
{
    if (peerKey.isEmpty() || peerKey.startsWith(QLatin1String("secret:"))) return false;
    return m_session->peers().info(TgPeer::fromKey(peerKey)).isForum;
}

void AppController::searchPeers(const QString &query)
{
    // Forward picker: existing chats matched locally (instant), then a server search for other
    // people appended when it returns. Empty query = every existing chat.
    QString q = query.trimmed();
    m_peerSearchQuery = q;
    QString ql = q.toLower();
    m_peerSearchResults.clear();
    QSet<QString> seen;
    for (int i = 0; i < m_chats->rowCount(); ++i) {
        QVariantMap row = m_chats->get(i);
        if (row.value(QLatin1String("secret")).toBool()) continue;   // can't forward to a secret chat
        QString key = row.value(QLatin1String("peerKey")).toString();
        QString title = row.value(QLatin1String("title")).toString();
        if (key.isEmpty() || seen.contains(key)) continue;
        if (!ql.isEmpty() && !title.toLower().contains(ql)) continue;
        QVariantMap e;
        e[QLatin1String("peerKey")] = key;
        e[QLatin1String("title")] = title;
        e[QLatin1String("subtitle")] = row.value(QLatin1String("subtitle"));
        e[QLatin1String("local")] = true;
        m_peerSearchResults.append(e);
        seen.insert(key);
    }
    emit peerSearchChanged();
    if (!q.isEmpty() && m_session->isOnline()) m_session->searchPeers(q);
}

void AppController::onPeersFound(const QString &query, const QList<TgPeer> &peers)
{
    if (query != m_peerSearchQuery) return;   // a late reply for a query the user has moved on from
    QSet<QString> seen;
    for (int i = 0; i < m_peerSearchResults.size(); ++i)
        seen.insert(m_peerSearchResults.at(i).toMap().value(QLatin1String("peerKey")).toString());
    for (int i = 0; i < peers.size(); ++i) {
        QString key = peers.at(i).key();
        if (key.isEmpty() || key.startsWith(QLatin1String("secret:")) || seen.contains(key)) continue;
        QVariantMap e;
        e[QLatin1String("peerKey")] = key;
        e[QLatin1String("title")] = m_session->peers().title(peers.at(i));
        e[QLatin1String("subtitle")] = QString();
        e[QLatin1String("local")] = false;
        m_peerSearchResults.append(e);
        seen.insert(key);
    }
    emit peerSearchChanged();
}

void AppController::startSecretChat(const QString &peerKey)
{
    TgPeer p = TgPeer::fromKey(peerKey);
    if (p.kind != TgPeer::User) { setNotice(tr("Secret chats can only be started with a person.")); return; }
    m_session->requestSecretChat(p);
    setNotice(tr("Starting a secret chat..."));
}

void AppController::onSecretRequested(int id, qint64 adminId)
{
    Q_UNUSED(id);
    QString who = m_session->peers().userName(adminId);
    setNotice(who.isEmpty() ? tr("Someone wants to start a secret chat.")
                            : tr("%1 wants to start a secret chat.").arg(who));
    if (!appInForeground() && notifications())
        m_notifier->alert(tr("Secret chat"), tr("%1 wants to start a secret chat").arg(who),
                          popupMode() != 0, soundMode() != 0, vibrationMode() != 0);
}

void AppController::onSecretReady(int id)
{
    Q_UNUSED(id);
    setNotice(tr("Secret chat is ready."));
}

void AppController::onSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out, int ttl)
{
    Q_UNUSED(randomId); Q_UNUSED(date); Q_UNUSED(ttl);
    if (out) return;
    const QString key = QLatin1String("secret:") + QString::number(id);
    const bool foreground = appInForeground();
    if (foreground && m_chat->peerKey() == key) return;
    if (foreground) return;
    if (!notifications()) return;               // global switch (secret chats are never archived/muted in v1)
    TgSecretChat sc = m_session->secretChat(id);
    QString who = m_session->peers().userName(sc.peerUserId);
    m_notifier->alert(who.isEmpty() ? tr("Secret chat") : who, text.isEmpty() ? tr("Encrypted message") : text,
                      popupMode() != 0, soundMode() != 0, vibrationMode() != 0);
    m_notifier->setPendingCount(m_notifier->pendingCount() + 1);
}

void AppController::onResolveFailed(const QString &error)
{
    setNotice(error);
}

QString AppController::pickAttachment(bool asPhoto)
{
    if (!m_session->isOnline()) { setNotice(tr("Not connected.")); return QString(); }
    if (m_chat->peerKey().isEmpty()) return QString();
    QString filter = asPhoto ? tr("Images (*.jpg *.jpeg *.png *.gif *.bmp)") : tr("All files (*)");
    return QFileDialog::getOpenFileName(0, asPhoto ? tr("Choose an image") : tr("Choose a file"), QString(), filter);
}

void AppController::sendAttachment(const QString &path, bool asPhoto, const QString &caption)
{
    if (path.isEmpty() || m_chat->peerKey().isEmpty()) return;
    qint64 randomId = m_session->sendFile(m_chat->peer(), path, asPhoto, caption.trimmed());
    m_chat->noteOutgoingMedia(randomId, path, asPhoto, caption.trimmed());
    setNotice(asPhoto ? tr("Sending the image...") : tr("Sending the file..."));
}

void AppController::openUrl(const QString &url)
{
    QDesktopServices::openUrl(QUrl(url));
}

void AppController::copyText(const QString &text)
{
    QApplication::clipboard()->setText(text);
    setNotice(tr("Copied."));
}

void AppController::appendLog(const QString &line)
{
    if (!loggingEnabled) return;
    QStringList &lines = logLines();
    lines.append(QDateTime::currentDateTime().toString(QLatin1String("HH:mm:ss ")) + line);
    while (lines.size() > 40) lines.removeFirst();
    if (logOwner) emit logOwner->logChanged();
}

QString AppController::logTail() const
{
    return logLines().join(QLatin1String("\n"));
}

bool AppController::autotest() const
{
    return !qgetenv("SGM_SHOT_DIR").isEmpty();
}

QString AppController::autotestPeer() const
{
    return QString::fromLocal8Bit(qgetenv("SGM_SHOT_PEER"));
}

bool AppController::autotestFolder() const
{
    return !qgetenv("SGM_SHOT_FOLDER").isEmpty();
}

void AppController::takeScreenshot(const QString &name)
{
    QByteArray dir = qgetenv("SGM_SHOT_DIR");
    if (dir.isEmpty() || !m_view) return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    QPixmap::grabWidget(m_view).save(QString::fromLocal8Bit(dir) + QLatin1String("/") + name + QLatin1String(".png"));
}
