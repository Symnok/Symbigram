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
#include "secretchat.h"
#include "srp.h"
#include "tgapi.h"
#include "tgtypes.h"

#include <QHash>
#include <QList>
#include <QNetworkProxy>
#include <QSet>
#include <QObject>
#include <QString>

class QFile;
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
    /// Route all connections through a SOCKS5 proxy (or none when host is empty).
    void setProxy(bool enabled, const QString &host, int port, const QString &user, const QString &pass);
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
    const QList<TgDialog> &archivedDialogs() const { return m_archived; }
    const QList<TgFolder> &folders() const { return m_folders; }
    bool archiveHasMore() const { return m_archiveHasMore; }
    bool isArchived(const TgPeer &peer) const { return m_archivedPeers.contains(peer.key()); }
    void loadArchive();
    void loadMoreArchive();
    bool dialogsHaveMore() const { return m_dialogsHaveMore; }
    bool dialogsLoading() const { return m_dialogsLoading; }
    TgDialog dialog(const TgPeer &peer) const;
    qint64 selfId() const { return m_selfId; }
    QString selfName() const;
    TgPeerInfo selfInfo() const { return m_peers.info(TgPeer(TgPeer::User, m_selfId)); }

    void refreshDialogs();
    void loadMoreDialogs();
    void loadFolders();
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
    /// Removes the whole chat (not just its messages). forEveryone revokes / leaves.
    void deleteChat(const TgPeer &peer, bool forEveryone);
    /// Moves a chat into the Archive (archived=true) or back to the main list.
    void archiveChat(const TgPeer &peer, bool archived);
    /// Adds/removes a chat from a custom folder (filterId), or removes from all when filterId<=0.
    void moveToFolder(const TgPeer &peer, int filterId, bool remove);
    /// "Move" a chat so it sits in exactly destFilterId (removed from all other custom folders); -1 = no folder.
    void setChatFolder(const TgPeer &peer, int destFilterId);
    void deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke);
    void setMuted(const TgPeer &peer, bool muted);
    /// Adds a resolved peer to the dialog list (locally) so a chat can be opened with it.
    void ensureDialog(const TgPeer &peer);
    /// Persists the update position; called by the app before it quits.
    void saveState();

    // -- secret (end-to-end) chats --
    QList<TgSecretChat> secretChats() const;
    TgSecretChat secretChat(int id) const;
    /// Opens a secret chat with a user (must be a known user with an access hash).
    void requestSecretChat(const TgPeer &user);
    void acceptSecretChat(int id);
    void discardSecretChat(int id);
    /// Returns the random id that secretMessageSent/secretMessageFailed will carry.
    qint64 sendSecretText(int id, const QString &text);
    void setSecretTtl(int id, int seconds);
    /// The in-memory message buffer of a secret chat (device-local, lost on restart).
    QList<TgSecretMsg> secretHistory(int id) const { return m_secretHistory.value(id); }
    /// Called when a self-destructing message is first shown, to start its countdown.
    void startSecretExpiry(int id, qint64 randomId);
    QByteArray secretKeyHash(int id) const;

    // -- files --
    /// Fetches an attachment (a photo size, or a document with an empty sizeType) into
    /// targetPath; the job id identifies the downloadProgress/Finished/Failed signals.
    int downloadFile(const TgMedia &media, const QString &sizeType, const QString &targetPath);
    /// A profile / chat picture (the small one).
    int downloadPeerPhoto(const TgPeer &peer, qint64 photoId, int dcId, const QString &targetPath);
    void cancelDownload(int jobId);
    /// Uploads a file and sends it, as a photo or as a document; returns the random id
    /// that uploadProgress and messageSent/messageFailed will carry.
    qint64 sendFile(const TgPeer &peer, const QString &filePath, bool asPhoto, const QString &caption);
    /// Sends an already-recorded Ogg/Opus file as a voice message.
    qint64 sendVoice(const TgPeer &peer, const QString &oggPath, int durationSec, const QByteArray &waveform);

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
    void archiveChanged();
    void foldersChanged();
    void dialogChanged(const TgPeer &peer);
    void historyLoaded(const TgPeer &peer, const QList<TgMessage> &messages, int offsetId, bool more);
    void historyFailed(const TgPeer &peer, const QString &error);
    void messageReceived(const TgMessage &message);
    void messageEdited(const TgMessage &message);
    void messagesDeleted(const TgPeer &peer, const QList<int> &ids);
    /// The server's version of a message we sent (id, date, and the media for uploads).
    void messageSent(const TgPeer &peer, qint64 randomId, const TgMessage &message);
    void downloadProgress(int jobId, qint64 received, qint64 total);
    void downloadFinished(int jobId, const QString &path);
    void downloadFailed(int jobId, const QString &error);
    void uploadProgress(qint64 randomId, qint64 sent, qint64 total);
    void messageFailed(const TgPeer &peer, qint64 randomId, const QString &error);
    void typing(const TgPeer &peer, qint64 userId);
    void peerChanged(const TgPeer &peer);
    void readInbox(const TgPeer &peer, int maxId);
    void readOutbox(const TgPeer &peer, int maxId);
    void peerResolved(const TgPeer &peer);
    void resolveFailed(const QString &error);
    void notice(const QString &text);
    void log(const QString &line);
    // secret chats
    void secretChatsChanged();
    void secretChatRequested(int id, qint64 userId);
    void secretChatReady(int id);
    void secretChatDiscarded(int id);
    void secretMessageReceived(int id, qint64 randomId, const QString &text, int date, bool out, int ttl);
    void secretMessageExpired(int id, qint64 randomId);
    void secretMessageSent(int id, qint64 randomId, int date);
    void secretMessageFailed(int id, qint64 randomId, const QString &error);

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
    void onSecretExpiryTick();
    void onLogOutTimeout();
    void onDcConnected();
    void onDcDisconnected(const QString &reason);

private:
    enum Kind {
        ExportToken, ImportToken, GetPassword, CheckPassword, LogOut,
        GetSelf, GetState, GetDifference, GetDialogs, GetHistory, SendMessage,
        ReadHistory, SetTyping, UpdateStatus, ResolveUsername, ResolvePhone, ContactsSearch,
        DeleteHistory, DeleteMessages, UpdateNotifySettings, GetUser,
        GetFile, SaveFilePart, SendMedia, ExportAuthorization, ImportAuthorization,
        GetArchive, GetFolders,
        GetDhConfig, RequestEncryption, AcceptEncryption, SendEncrypted, DiscardEncryption,
        ArchivePeer, DeleteChat, MoveFolder
    };
    struct Request
    {
        Request() : kind(GetSelf), randomId(0), offsetId(0), more(false), folderId(0), secretChatId(0), secret(0), revoke(false) {}
        Kind kind;
        TgPeer peer;
        qint64 randomId;
        int offsetId;
        bool more;              // GetDialogs: appending a page rather than replacing
        int folderId;           // GetDialogs/GetArchive: which folder
        int secretChatId;       // secret-chat requests
        bool revoke;            // DeleteChat: delete for everyone / leave-and-revoke
        SecretChat *secret;     // RequestEncryption: the in-progress chat awaiting its id
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
    void requestDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, bool more, int folderId);
    void applyDialogs(const TgDialogPage &page, bool more);
    void applyArchive(const TgDialogPage &page, bool more);
    void setArchived(const TgPeer &peer, bool archived);
    void removeDialogLocal(const TgPeer &peer);
    void applyUpdatesResult(const TlObject &o);
    QByteArray buildFolderFilter(const TgFolder &f, const TgPeer &addPeer, const TgPeer &removePeer) const;
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

    // -- secret chats --
    void ensureDhConfig();
    void handleEncryptedChat(const TlObject &chat);
    void handleEncryptedMessage(const TlObject &message, int date);
    void sendSecretService(SecretChat *chat, const QByteArray &body);
    void appendSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out, int ttl);
    void ensureSecretExpiryTimer();
    void loadSecrets();
    void saveSecrets();
    QString secretFilePath() const;
    TgSecretChat lightSecret(const SecretChat *c) const;

    // -- files --
    struct Download
    {
        Download() : jobId(0), dcId(0), offset(0), total(0), file(0), active(false), migrations(0) {}
        int jobId;
        int dcId;
        QByteArray location;
        qint64 offset, total;
        QString path;
        QFile *file;
        bool active;
        int migrations;
    };
    struct Upload
    {
        Upload() : randomId(0), file(0), fileId(0), parts(0), nextPart(0), size(0), big(false), asPhoto(false), voice(false), durationSec(0) {}
        qint64 randomId;
        TgPeer peer;
        QString path, fileName, caption;
        QFile *file;
        qint64 fileId;
        int parts, nextPart;
        qint64 size;
        bool big, asPhoto;
        bool voice;
        int durationSec;
        QByteArray waveform;
    };
    struct DcLink
    {
        DcLink() : client(0), dcId(0), authorized(false), importing(false) {}
        MtprotoClient *client;
        int dcId;
        bool authorized;
        bool importing;
    };
    quint64 sendOn(MtprotoClient *client, Kind kind, const QByteArray &body, const Request &req);
    MtprotoClient *clientForDc(int dcId);
    int addDownload(int dcId, const QByteArray &location, const QString &targetPath, qint64 total);
    void pumpDownloads();
    void requestChunk(Download &d, MtprotoClient *client);
    void finishDownload(int jobId, const QString &error);
    void failTransfersOnDc(int dcId, const QString &error);
    void failAllTransfers(const QString &error);
    void sendNextPart(Upload &u);
    void finishUpload(qint64 randomId, const QString &error);
    void dcAuthorized(int dcId);

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
    QNetworkProxy m_proxy;
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
    QList<TgDialog> m_archived;
    QSet<QString> m_archivedPeers;
    QList<TgFolder> m_folders;
    bool m_archiveHasMore;
    bool m_archiveLoaded;
    bool m_dialogsHaveMore;
    bool m_dialogsLoading;
    bool m_differencePending;
    bool m_online;

    QHash<int, AuthKey> m_dcKeys;          // keys for the other datacenters, persisted
    QHash<int, DcLink> m_dcLinks;
    QHash<int, Download> m_downloads;
    QList<int> m_downloadOrder;
    QHash<qint64, Upload> m_uploads;
    int m_nextJobId;
    QList<QString> m_recentSeen;   // "peerkey:id" of the last messages delivered, for dedup

    // secret chats
    QHash<int, SecretChat *> m_secretChats;
    int m_dhG;
    QByteArray m_dhP;
    int m_dhVersion;
    bool m_dhReady;
    QList<TgPeer> m_secretRequestQueue;   // peers waiting for the DH config to request
    QList<int> m_secretAcceptQueue;       // chat ids waiting for the DH config to accept
    QHash<int, QList<TgSecretMsg> > m_secretHistory;  // secret messages, in memory only
    QHash<int, QSet<qint64> > m_secretSeen;          // random ids already delivered (redelivery dedup)
    QTimer *m_secretExpiryTimer;                     // sweeps self-destructing messages once a second
};

#endif // TELEGRAMSESSION_H
