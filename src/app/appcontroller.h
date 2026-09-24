// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Process-wide state, exposed to QML as "app": the network session (the Symbian access
// point), the Telegram session with automatic reconnection, the models the pages bind to,
// settings, notifications, and the little host services QML cannot do itself.
#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

class TelegramSession;
class ChatsModel;
class MessagesModel;
class Notifier;
class MediaCache;
class QNetworkConfigurationManager;
class VoiceRecorder;
class QNetworkSession;
class QTimer;
class QDeclarativeView;
struct TgMessage;
struct TgPeer;

class AppController : public QObject
{
    Q_OBJECT
    /// "starting" (opening the network), "login" (QR page) or "ready" (chat list).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    /// "offline", "connecting" or "online" - the Telegram link, shown on the chat list.
    Q_PROPERTY(QString connection READ connection NOTIFY connectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// The login page: what is going on, the QR token (for image://qr/), the password step.
    Q_PROPERTY(QString loginStatus READ loginStatus NOTIFY loginChanged)
    Q_PROPERTY(QString loginError READ loginError NOTIFY loginChanged)
    Q_PROPERTY(QString qrToken READ qrToken NOTIFY loginChanged)
    Q_PROPERTY(int qrExpires READ qrExpires NOTIFY loginChanged)
    Q_PROPERTY(bool passwordNeeded READ passwordNeeded NOTIFY loginChanged)
    Q_PROPERTY(QString passwordHint READ passwordHint NOTIFY loginChanged)
    Q_PROPERTY(bool checkingPassword READ checkingPassword NOTIFY loginChanged)
    Q_PROPERTY(QString loginMethod READ loginMethod NOTIFY loginChanged)
    Q_PROPERTY(bool codeNeeded READ codeNeeded NOTIFY loginChanged)
    Q_PROPERTY(QString loginPhone READ loginPhone NOTIFY loginChanged)
    Q_PROPERTY(bool codeBusy READ codeBusy NOTIFY loginChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool vibrate READ vibrate WRITE setVibrate NOTIFY settingsChanged)
    Q_PROPERTY(bool popups READ popups WRITE setPopups NOTIFY settingsChanged)
    Q_PROPERTY(bool groupNotifications READ groupNotifications WRITE setGroupNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool logging READ logging WRITE setLogging NOTIFY settingsChanged)
    Q_PROPERTY(QStringList downloadDrives READ downloadDrives NOTIFY settingsChanged)
    Q_PROPERTY(int downloadDriveIndex READ downloadDriveIndex WRITE setDownloadDriveIndex NOTIFY settingsChanged)
    Q_PROPERTY(QString downloadFolder READ downloadFolder NOTIFY settingsChanged)
    Q_PROPERTY(bool downloadCustom READ downloadCustom NOTIFY settingsChanged)
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyPort READ proxyPort NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyUser READ proxyUser NOTIFY settingsChanged)
    Q_PROPERTY(QString proxyPass READ proxyPass NOTIFY settingsChanged)
    Q_PROPERTY(QString myName READ myName NOTIFY selfChanged)
    Q_PROPERTY(QString mySubtitle READ mySubtitle NOTIFY selfChanged)
    Q_PROPERTY(ChatsModel *chats READ chats CONSTANT)
    Q_PROPERTY(MessagesModel *chat READ chat CONSTANT)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString cacheSize READ cacheSize NOTIFY cacheChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    /// Desktop testing: true when SGM_SHOT_DIR is set; main.qml then walks the pages.
    Q_PROPERTY(bool autotest READ autotest CONSTANT)
    Q_PROPERTY(QString autotestPeer READ autotestPeer CONSTANT)
    Q_PROPERTY(bool autotestFolder READ autotestFolder CONSTANT)
    /// The last log lines for the About page.
    Q_PROPERTY(QString logTail READ logTail NOTIFY logChanged)
public:
    explicit AppController(QObject *parent = 0);
    ~AppController();

    void setView(QDeclarativeView *view) { m_view = view; }

    QString state() const { return m_state; }
    QString connection() const;
    bool busy() const { return m_busy; }
    QString loginStatus() const;
    QString loginError() const { return m_loginError; }
    QString qrToken() const;
    int qrExpires() const;
    bool passwordNeeded() const;
    QString passwordHint() const;
    bool checkingPassword() const { return m_checkingPassword; }
    QString loginMethod() const { return m_loginMethod; }     // "qr" or "phone"
    bool codeNeeded() const;
    QString loginPhone() const;
    bool codeBusy() const { return m_codeBusy; }
    QString version() const;
    QString language() const;
    void setLanguage(const QString &lang);
    bool notifications() const;
    void setNotifications(bool on);
    bool vibrate() const;
    void setVibrate(bool on);
    bool popups() const;
    void setPopups(bool on);
    bool groupNotifications() const;
    void setGroupNotifications(bool on);
    bool autoConnect() const;
    void setAutoConnect(bool on);
    bool logging() const;
    void setLogging(bool on);
    QStringList downloadDrives() const;            // friendly labels of the present drives
    int downloadDriveIndex() const;                // index into downloadDrives()
    void setDownloadDriveIndex(int index);
    QString downloadFolder() const;                // the resolved path (custom, or <drive>/.../Symbigram)
    bool downloadCustom() const;                   // true if a folder was picked instead of a drive
    bool proxyEnabled() const;
    QString proxyHost() const;
    QString proxyPort() const;
    QString proxyUser() const;
    QString proxyPass() const;
    QString myName() const;
    QString mySubtitle() const;
    ChatsModel *chats() const { return m_chats; }
    MessagesModel *chat() const { return m_chat; }
    QString notice() const { return m_notice; }
    QString cacheSize() const;                     // human-readable size of the media cache
    bool autotest() const;
    QString autotestPeer() const;
    bool autotestFolder() const;
    QString logTail() const;
    static void appendLog(const QString &line);

    /// Opens the network, then connects with the saved session or shows the QR page.
    void start();
    static QString effectiveLanguage(const QSettings &settings);

    /// Watches the application coming to the foreground (clears the panel notification,
    /// tells Telegram we are online).
    bool eventFilter(QObject *watched, QEvent *event);

public slots:
    void clearCache();                             // empties the media cache; notice() reports the result
    void checkPassword(const QString &password);
    // Phone-number login (an alternative to the QR code).
    void usePhoneLogin();                         // switch the login page to the phone form
    void useQrLogin();                            // switch back to the QR code
    void sendLoginCode(const QString &phone);     // request the SMS/app code
    void submitLoginCode(const QString &code);    // sign in with the typed code
    void resendLoginCode();                       // ask Telegram to send the code again
    void changeLoginNumber();                     // go back to the phone-number field
    void signOut();
    void reconnect();
    void goOffline();
    /// Username, phone number or a name; peerResolved opens the chat.
    void findPeer(const QString &query);
    /// Opens a file picker and sends the chosen file to the open chat, as a photo or a
    /// document. Nothing happens if the user cancels.
    /// Two-step attach so a caption can be added after the file is chosen: pickAttachment opens
    /// the file picker and returns the path (empty if cancelled); sendAttachment sends it.
    QString pickAttachment(bool asPhoto);
    void sendAttachment(const QString &path, bool asPhoto, const QString &caption);
    /// Voice messages: start/stop capture; stop encodes and sends to the open chat.
    void startRecording();
    void stopRecording();
    void cancelRecording();
    bool recording() const;
    Q_INVOKABLE int recordingMs() const;
    /// Starts an end-to-end secret chat with the person of an existing 1:1 chat.
    void startSecretChat(const QString &peerKey);
    /// Opens a native folder picker so the user can choose any download folder.
    void chooseDownloadFolder();
    /// Store SOCKS5 proxy settings, apply them, and reconnect so they take effect.
    void saveProxy(bool enabled, const QString &host, const QString &port, const QString &user, const QString &pass);
    void setProxyEnabled(bool on);
    void openUrl(const QString &url);
    void copyText(const QString &text);
    void clearNotice();
    /// Desktop testing: screenshots of the pages (SGM_SHOT_DIR).
    void takeScreenshot(const QString &name);

signals:
    void stateChanged();
    void connectionChanged();
    void busyChanged();
    void loginChanged();
    void settingsChanged();
    void selfChanged();
    void noticeChanged();
    void cacheChanged();
    void recordingChanged();
    /// A chat was found by findPeer; the list page opens it.
    void peerFound(const QString &peerKey);
    void logChanged();

private slots:
    void onNetworkOpened();
    void onNetworkError();
    void onSessionState();
    void onSessionDisconnected(const QString &reason);
    void onSignedIn();
    void onSignedOut(const QString &reason);
    void onLoginError(const QString &error);
    void onLoginChanged();
    void onMessage(const TgMessage &message);
    void onPeerResolved(const TgPeer &peer);
    void onResolveFailed(const QString &error);
    void onReconnectTimer();
    void onSessionLog(const QString &line);
    void onNotice(const QString &text);
    void onRecorded(const QString &oggPath, int durationSec, const QByteArray &waveform);
    void onRecordFailed(const QString &error);
    void onSecretRequested(int id, qint64 adminId);
    void onSecretReady(int id);
    void onSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out, int ttl);

private:
    void setState(const QString &s);
    void setBusy(bool b);
    void setNotice(const QString &n);
    void continueStart();
    void connectSession();
    bool appInForeground() const;
    static QString dataDir();
    static QStringList presentDriveLetters();      // among C:, E:, F: which exist
    static QString folderForDrive(const QString &drive);
    void applyDownloadFolder();                    // create it + hand it to the chat model
    void applyProxy();                             // push current settings to the session + reconnect

    QSettings m_settings;
    TelegramSession *m_session;
    ChatsModel *m_chats;
    MessagesModel *m_chat;
    Notifier *m_notifier;
    MediaCache *m_media;
    VoiceRecorder *m_recorder;
    QString m_downloadPath;   // the chosen public folder (recordings, WAV)
    QNetworkConfigurationManager *m_netMgr;
    QNetworkSession *m_netSession;
    QTimer *m_reconnect;
    QDeclarativeView *m_view;
    QString m_state;
    bool m_busy;
    QString m_loginError;
    QString m_notice;
    bool m_wantOnline;
    bool m_everOnline;
    int m_reconnectDelay;
    bool m_checkingPassword;
    QString m_loginMethod;    // "qr" (default) or "phone"
    bool m_codeBusy;          // a send-code / sign-in request is in flight
    bool m_foreground;
};

#endif // APPCONTROLLER_H
