// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The open chat for QML: its history oldest-first (loaded from the server as the chat
// opens, older pages on request), sending with delivery and read marks, typing in both
// directions, and the peer's presence for the header.
#ifndef MESSAGESMODEL_H
#define MESSAGESMODEL_H

#include "tgtypes.h"

class MediaCache;
class VoicePlayer;

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QTime>
#include <QVariantMap>

class TelegramSession;
class QTimer;

class MessagesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString peerKey READ peerKey NOTIFY chatChanged)
    Q_PROPERTY(QString title READ title NOTIFY peerChanged)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY peerChanged)
    Q_PROPERTY(bool peerTyping READ peerTyping NOTIFY peerChanged)
    Q_PROPERTY(bool peerIsGroup READ peerIsGroup NOTIFY chatChanged)
    Q_PROPERTY(bool peerIsChannel READ peerIsChannel NOTIFY chatChanged)
    Q_PROPERTY(bool peerMuted READ peerMuted NOTIFY peerChanged)
    Q_PROPERTY(QString initials READ initials NOTIFY peerChanged)
    Q_PROPERTY(QString color READ color NOTIFY chatChanged)
    Q_PROPERTY(QString avatar READ avatar NOTIFY peerChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)
    Q_PROPERTY(bool isSecret READ isSecret NOTIFY chatChanged)
    Q_PROPERTY(int secretState READ secretState NOTIFY peerChanged)      // 0 by-me, 1 to-me, 2 ready
    Q_PROPERTY(int secretTtl READ secretTtl NOTIFY peerChanged)
public:
    enum Roles {
        MsgIdRole = Qt::UserRole + 1,
        BodyRole,
        NoteRole,
        OutRole,
        SenderRole,
        TimeTextRole,
        DateTextRole,
        ShowDateRole,
        PendingRole,
        FailedRole,
        ReadRole,
        ServiceRole,
        ForwardedRole,
        ReplyRole,
        EditedRole,
        MediaKindRole,      // "", "photo", "video", "voice", "audio", "sticker", "gif", "document"
        MediaThumbRole,     // file:// url of a thumbnail/image to show, or ""
        MediaStateRole,     // "none", "idle", "loading", "ready", "failed"
        MediaProgressRole,  // 0..100
        MediaInfoRole,      // "1.2 MB", "0:12", "video 0:30" - the caption line for non-photos
        MediaWidthRole,
        MediaHeightRole,
        LocalPathRole,      // the full downloaded file, once it exists
        SecretBurnRole,     // true if this message self-destructs
        SecretRemainingRole,// seconds left before it self-destructs (-1 = none)
        VoicePlayingRole    // true while this voice message is playing
    };

    MessagesModel(TelegramSession *session, MediaCache *media, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    QString peerKey() const { return m_secretId ? (QLatin1String("secret:") + QString::number(m_secretId)) : (m_peer.isNull() ? QString() : m_peer.key()); }
    bool isSecret() const { return m_secretId != 0; }
    int secretState() const;
    int secretTtl() const;
    TgPeer peer() const { return m_peer; }
    QString title() const;
    QString subtitle() const;
    bool peerTyping() const;
    bool peerIsGroup() const { return m_peer.isGroup(); }
    bool peerIsChannel() const;
    bool peerMuted() const;
    QString initials() const;
    QString color() const;
    QString avatar() const;
    bool loading() const { return m_loading; }
    bool hasOlder() const { return m_hasOlder; }
    QString error() const { return m_error; }

    Q_INVOKABLE void open(const QString &peerKey);
    Q_INVOKABLE void close();
    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void retry(int row);
    Q_INVOKABLE void loadOlder();
    Q_INVOKABLE void markRead();
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Called by the composer as the text changes: drives the typing notification.
    Q_INVOKABLE void composing(const QString &text);
    Q_INVOKABLE void setMuted(bool muted);
    /// Secret chats: accept an incoming request, close/discard, change TTL, and the
    /// key fingerprint (SHA-256 of the shared key) as spaced hex for the verify screen.
    Q_INVOKABLE void acceptSecret();
    Q_INVOKABLE void discardSecret();
    Q_INVOKABLE void setSecretTtl(int seconds);
    Q_INVOKABLE QString secretKeyHex() const;
    Q_INVOKABLE void deleteMessage(int row, bool forEveryone);
    /// Photos: fetch the size to save and copy it out. Documents/video/etc.: fetch the
    /// whole file. Drives MediaStateRole/MediaProgressRole for the row.
    Q_INVOKABLE void downloadMedia(int row);
    /// Copies the (downloaded) attachment to a user-visible folder; notice() reports where.
    Q_INVOKABLE void saveMedia(int row);
    /// Opens the downloaded attachment with the phone's handler for its type.
    Q_INVOKABLE void openMedia(int row);
    /// Play or pause a voice message (downloads it first if needed).
    Q_INVOKABLE void playVoice(int row);
    Q_INVOKABLE bool canDeleteForEveryone(int row) const;
    /// Adds an optimistic outgoing row for a file being uploaded (matched later by random id).
    void noteOutgoingMedia(qint64 randomId, const QString &localPath, bool asPhoto);
    /// AppController sets this to the user-chosen drive folder; empty = platform default.
    void setDownloadFolder(const QString &dir) { m_downloadFolder = dir; }

signals:
    void chatChanged();
    void peerChanged();
    void countChanged();
    void loadingChanged();
    void messageAppended();
    void olderPrepended(int count);
    void sendFailed(const QString &error);
    void mediaSaved(const QString &path);   // a file was copied out; path is where

private slots:
    void onHistoryLoaded(const TgPeer &peer, const QList<TgMessage> &messages, int offsetId, bool more);
    void onHistoryFailed(const TgPeer &peer, const QString &error);
    void onMessageReceived(const TgMessage &m);
    void onMessageEdited(const TgMessage &m);
    void onMessagesDeleted(const TgPeer &peer, const QList<int> &ids);
    void onMessageSent(const TgPeer &peer, qint64 randomId, const TgMessage &message);
    void onMessageFailed(const TgPeer &peer, qint64 randomId, const QString &error);
    void onTyping(const TgPeer &peer, qint64 userId);
    void onTypingIdle();
    void onPeerTypingIdle();
    void onPeerChanged(const TgPeer &peer);
    void onReadOutbox(const TgPeer &peer, int maxId);
    void onSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out, int ttl);
    void onSecretExpired(int id, qint64 randomId);
    void onBurnTick();
    void onSecretChatsChanged();
    void onMediaReady(const QString &key, const QString &path);
    void onMediaFailed(const QString &key, const QString &error);
    void onMediaProgress(const QString &key, int percent);
    void onVoiceStopped();

private:
    struct Row
    {
        Row() : randomId(0), pending(false), failed(false), mediaLoading(false), mediaFailed(false), progress(0), ttl(0), expiresAt(0) {}
        TgMessage m;
        qint64 randomId;      // outgoing: for matching the send result
        bool pending;
        bool failed;
        QString thumbPath;    // a small image to show (stripped preview, then the fetched size)
        QString fullPath;     // the whole file, once downloaded
        QString awaitKey;     // the cache key this row is currently waiting on
        bool mediaLoading;
        bool mediaFailed;
        int progress;
        int ttl;              // secret self-destruct seconds (0 = none)
        int expiresAt;        // unix time it self-destructs (0 = not started)
    };
    void prepareMedia(Row &r);
    void openSecret(int id);
    void startVoice(int row);
    bool anyBurning() const;   // any visible message counting down?
    /// The user-visible folder saved files go to (drive-aware on Symbian).
    static QString downloadDir(bool photo);
    int rowByKey(const QString &key) const;
    static QString mediaKindName(TgMedia::Kind k);
    QString mediaInfoText(const TgMedia &m) const;
    int rowById(int id) const;
    int rowByRandomId(qint64 randomId) const;
    void sendRow(int row);
    static QString timeText(int unixTime);
    static QString dateText(int unixTime);
    QString lastSeenText(const TgPeerInfo &info) const;

    TelegramSession *m_session;
    MediaCache *m_media;
    VoicePlayer *m_voice;
    int m_voiceRow;        // the row currently playing, or -1
    int m_pendingPlayRow;  // a voice row to play as soon as its download finishes
    TgPeer m_peer;
    int m_secretId;
    QString m_downloadFolder;   // where "Save" copies files (chosen in Settings)          // non-zero when the open chat is a secret (end-to-end) chat
    QList<Row> m_rows;
    bool m_loading;
    bool m_hasOlder;
    QString m_error;
    int m_readOutboxMaxId;
    QTimer *m_typingTimer;
    QTimer *m_peerTypingTimer;
    QTimer *m_burnTimer;                    // ticks the self-destruct countdowns while a secret chat is open
    bool m_typingSent;
    QTime m_typingSentAt;
    qint64 m_peerTypingUser;
};

#endif // MESSAGESMODEL_H
