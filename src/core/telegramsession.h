// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The account's session with Telegram: the stored authorisation key and datacenter, the
// QR login flow (with the datacenter migration and the two-step password), the dialog
// list, message history, sending, read marks, typing, and the update stream that keeps it
// all current. Everything the UI needs is a signal here; nothing here knows about QML.
// Shared with the desktop harness (tools/symbigram-cli), which is how the protocol was
// verified against live Telegram before the phone build.
#ifndef TELEGRAMSESSION_H
#define TELEGRAMSESSION_H

#include "authkeyhandshake.h"
#include "mtprotoclient.h"
#include "srp.h"
#include "tgapi.h"
#include "tgtypes.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QTimer;

class TelegramSession : public QObject
{
    Q_OBJECT
public:
    enum State {
        Disconnected,      ///< no link
        Connecting,        ///< socket / key exchange in progress
        LoggingIn,         ///< connected, showing the QR code (or asking for the password)
        Syncing,           ///< signed in, loading state and dialogs
        Online             ///< signed in and current
    };

    explicit TelegramSession(QObject *parent = 0);
    ~TelegramSession();

    void setClientInfo(const ClientInfo &info) { m_info = info; }
    /// Where the authorisation key and the update position live. Loaded immediately.
    void setSessionFile(const QString &path);
    bool isSignedIn() const { return m_signedIn; }
    State state() const { return m_state; }
    bool isOnline() const { return m_state == Online; }
    int dcId() const { return m_dcId; }

    /// Opens the link: with a signed-in session it syncs, otherwise it starts the QR login.
    void connectToServer();
    void disconnectFromServer();

    // -- login --
    QString qrUrl() const { return m_qrUrl; }
    int qrExpires() const { return m_qrExpires; }
    bool passwordNeeded() const { return m_passwordNeeded; }
    QString passwordHint() const { return m_passwordHint; }
    void checkPassword(const QString &password);
    /// Ends the authorisation on the server and forgets it here.
    void logOut();

    // -- data --
    TgPeerCache &peers() { return m_peers; }
    const TgPeerCache &peers() const { return m_peers; }
    const QList<TgDialog> &dialogs() const { return m_dialogs; }
    bool dialogsHaveMore() const { return m_dialogsHaveMore; }
    bool dialogsLoading() const { return m_dialogsLoading; }
    TgDialog dialog(const TgPeer &peer) const;
    qint64 selfId() const { return m_selfId; }
    QString selfName() const;
    TgPeerInfo selfInfo() const { return m_peers.info(TgPeer(TgPeer::User, m_selfId)); }

    void refreshDialogs();
    void loadMoreDialogs();
    /// Recent messages of a chat; offsetId 0 = the newest, otherwise older than that id.
    void loadHistory(const TgPeer &peer, int offsetId, int count);
    /// Returns the random id that messageSent/messageFailed will carry.
    qint64 sendText(const TgPeer &peer, const QString &text, int replyToId = 0);
    void markRead(const TgPeer &peer, int maxId);
    void setTyping(const TgPeer &peer, bool typing);
    void setOnline(bool online);
    /// Username, phone number, t.me link, or a name to search among contacts.
    void resolve(const QString &query);
    void deleteHistory(const TgPeer &peer);
    void deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke);
    void setMuted(const TgPeer &peer, bool muted);
    /// Adds a resolved peer to the dialog list (locally) so a chat can be opened with it.
    void ensureDialog(const TgPeer &peer);
    /// Persists the update position; called by the app before it quits.
    void saveState();

signals:
    void stateChanged();
    /// The link failed or was lost, with the reason; the owner decides about reconnecting.
    void disconnected(const QString &reason);
    void qrChanged();
    void passwordNeededChanged();
    void loginError(const QString &error);
    void signedIn();
    /// The authorisation is gone (signed out here, or revoked elsewhere).
    void signedOut(const QString &reason);
    void selfChanged();
    void dialogsChanged();
    void dialogChanged(const TgPeer &peer);
    void historyLoaded(const TgPeer &peer, const QList<TgMessage> &messages, int offsetId, bool more);
    void historyFailed(const TgPeer &peer, const QString &error);
    void messageReceived(const TgMessage &message);
    void messageEdited(const TgMessage &message);
    void messagesDeleted(const TgPeer &peer, const QList<int> &ids);
    void messageSent(const TgPeer &peer, qint64 randomId, int msgId, int date);
    void messageFailed(const TgPeer &peer, qint64 randomId, const QString &error);
    void typing(const TgPeer &peer, qint64 userId);
    void peerChanged(const TgPeer &peer);
    void readInbox(const TgPeer &peer, int maxId);
    void readOutbox(const TgPeer &peer, int maxId);
    void peerResolved(const TgPeer &peer);
    void resolveFailed(const QString &error);
    void notice(const QString &text);
    void log(const QString &line);

private slots:
    void onConnected();
    void onDisconnected(const QString &reason);
    void onRpcResult(quint64 requestId, const QByteArray &result);
    void onRpcError(quint64 requestId, int code, const QString &type);
    void onUpdate(const TlObject &update);
    void onMovedConnected();
    void onMovedDisconnected(const QString &reason);
    void onQrPoll();
    void onSrpDone();
    void onSaveTimer();
    void onLogOutTimeout();

private:
    enum Kind {
        ExportToken, ImportToken, GetPassword, CheckPassword, LogOut,
        GetSelf, GetState, GetDifference, GetDialogs, GetHistory, SendMessage,
        ReadHistory, SetTyping, UpdateStatus, ResolveUsername, ResolvePhone, ContactsSearch,
        DeleteHistory, DeleteMessages, UpdateNotifySettings, GetUser
    };
    struct Request
    {
        Request() : kind(GetSelf), randomId(0), offsetId(0), more(false) {}
        Kind kind;
        TgPeer peer;
        qint64 randomId;
        int offsetId;
        bool more;              // GetDialogs: appending a page rather than replacing
        QString query;
    };

    void setState(State s);
    quint64 send(Kind kind, const QByteArray &body, const Request &req = Request());
    void loadSessionFile();
    void saveSessionFile();
    void forgetSession(const QString &reason);
    void startLogin();
    void exportToken();
    void handleLoginStep(const TgQrLoginStep &step, bool fromMovedDc);
    void adoptMoved();
    void finishLogin();
    void startSync();
    void requestDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, bool more);
    void applyDialogs(const TgDialogPage &page, bool more);
    void applyUpdate(const TlObject &u);
    void applyMessage(const TgMessage &m, bool fromDifference);
    bool checkPts(const TlObject &u);
    void getDifference();
    void touchDialog(const TgMessage &m);
    void fillSender(TgMessage &m) const;
    int dialogIndex(const TgPeer &peer) const;
    static int dialogIndexIn(const QList<TgDialog> &list, const TgPeer &peer);
    void sortDialogs();
    bool isAuthGone(const QString &type) const;
    void handleMigrate(const QString &type);
    static int unixNow();

    ClientInfo m_info;
    MtprotoClient *m_client;
    MtprotoClient *m_moved;            // the connection to the datacenter a login token migrated to
    QTimer *m_qrPoll;
    QTimer *m_saveTimer;
    SrpWorker *m_srp;
    QHash<quint64, Request> m_requests;
    State m_state;

    // the stored session
    QString m_sessionFile;
    AuthKey m_authKey;
    int m_dcId;
    bool m_signedIn;
    TgUpdateState m_updateState;
    bool m_stateDirty;

    // login
    QString m_qrUrl;
    int m_qrExpires;
    QByteArray m_qrToken;
    bool m_passwordNeeded;
    QString m_passwordHint;
    SrpParams m_srpParams;
    int m_movedDc;
    QByteArray m_movedToken;
    bool m_loggingOut;

    // data
    TgPeerCache m_peers;
    qint64 m_selfId;
    QList<TgDialog> m_dialogs;
    bool m_dialogsHaveMore;
    bool m_dialogsLoading;
    bool m_differencePending;
    bool m_online;
};

#endif // TELEGRAMSESSION_H
