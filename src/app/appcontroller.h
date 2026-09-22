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

class TelegramSession;
class ChatsModel;
class MessagesModel;
class Notifier;
class QNetworkConfigurationManager;
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
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool vibrate READ vibrate WRITE setVibrate NOTIFY settingsChanged)
    Q_PROPERTY(bool popups READ popups WRITE setPopups NOTIFY settingsChanged)
    Q_PROPERTY(bool groupNotifications READ groupNotifications WRITE setGroupNotifications NOTIFY settingsChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(QString myName READ myName NOTIFY selfChanged)
    Q_PROPERTY(QString mySubtitle READ mySubtitle NOTIFY selfChanged)
    Q_PROPERTY(ChatsModel *chats READ chats CONSTANT)
    Q_PROPERTY(MessagesModel *chat READ chat CONSTANT)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    /// Desktop testing: true when SGM_SHOT_DIR is set; main.qml then walks the pages.
    Q_PROPERTY(bool autotest READ autotest CONSTANT)
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
    QString myName() const;
    QString mySubtitle() const;
    ChatsModel *chats() const { return m_chats; }
    MessagesModel *chat() const { return m_chat; }
    QString notice() const { return m_notice; }
    bool autotest() const;
    QString logTail() const;
    static void appendLog(const QString &line);

    /// Opens the network, then connects with the saved session or shows the QR page.
    void start();
    static QString effectiveLanguage(const QSettings &settings);

    /// Watches the application coming to the foreground (clears the panel notification,
    /// tells Telegram we are online).
    bool eventFilter(QObject *watched, QEvent *event);

public slots:
    void checkPassword(const QString &password);
    void signOut();
    void reconnect();
    void goOffline();
    /// Username, phone number or a name; peerResolved opens the chat.
    void findPeer(const QString &query);
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

private:
    void setState(const QString &s);
    void setBusy(bool b);
    void setNotice(const QString &n);
    void continueStart();
    void connectSession();
    bool appInForeground() const;
    static QString dataDir();

    QSettings m_settings;
    TelegramSession *m_session;
    ChatsModel *m_chats;
    MessagesModel *m_chat;
    Notifier *m_notifier;
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
    bool m_foreground;
};

#endif // APPCONTROLLER_H
