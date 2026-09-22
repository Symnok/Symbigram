// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The open chat for QML: its history oldest-first (loaded from the server as the chat
// opens, older pages on request), sending with delivery and read marks, typing in both
// directions, and the peer's presence for the header.
#ifndef MESSAGESMODEL_H
#define MESSAGESMODEL_H

#include "tgtypes.h"

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
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasOlder READ hasOlder NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)
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
        EditedRole
    };

    MessagesModel(TelegramSession *session, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    QString peerKey() const { return m_peer.isNull() ? QString() : m_peer.key(); }
    TgPeer peer() const { return m_peer; }
    QString title() const;
    QString subtitle() const;
    bool peerTyping() const;
    bool peerIsGroup() const { return m_peer.isGroup(); }
    bool peerIsChannel() const;
    bool peerMuted() const;
    QString initials() const;
    QString color() const;
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
    Q_INVOKABLE void deleteMessage(int row, bool forEveryone);

signals:
    void chatChanged();
    void peerChanged();
    void countChanged();
    void loadingChanged();
    void messageAppended();
    void olderPrepended(int count);
    void sendFailed(const QString &error);

private slots:
    void onHistoryLoaded(const TgPeer &peer, const QList<TgMessage> &messages, int offsetId, bool more);
    void onHistoryFailed(const TgPeer &peer, const QString &error);
    void onMessageReceived(const TgMessage &m);
    void onMessageEdited(const TgMessage &m);
    void onMessagesDeleted(const TgPeer &peer, const QList<int> &ids);
    void onMessageSent(const TgPeer &peer, qint64 randomId, int msgId, int date);
    void onMessageFailed(const TgPeer &peer, qint64 randomId, const QString &error);
    void onTyping(const TgPeer &peer, qint64 userId);
    void onTypingIdle();
    void onPeerTypingIdle();
    void onPeerChanged(const TgPeer &peer);
    void onReadOutbox(const TgPeer &peer, int maxId);

private:
    struct Row
    {
        Row() : randomId(0), pending(false), failed(false) {}
        TgMessage m;
        qint64 randomId;      // outgoing: for matching the send result
        bool pending;
        bool failed;
    };
    int rowById(int id) const;
    int rowByRandomId(qint64 randomId) const;
    void sendRow(int row);
    static QString timeText(int unixTime);
    static QString dateText(int unixTime);
    QString lastSeenText(const TgPeerInfo &info) const;

    TelegramSession *m_session;
    TgPeer m_peer;
    QList<Row> m_rows;
    bool m_loading;
    bool m_hasOlder;
    QString m_error;
    int m_readOutboxMaxId;
    QTimer *m_typingTimer;
    QTimer *m_peerTypingTimer;
    bool m_typingSent;
    QTime m_typingSentAt;
    qint64 m_peerTypingUser;
};

#endif // MESSAGESMODEL_H
