// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The chat list for QML: the session's dialogs in server order (pinned first, then by the
// newest message), with the preview line, unread badges, mute marks, typing and presence.
#ifndef CHATSMODEL_H
#define CHATSMODEL_H

#include "tgtypes.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantMap>

class TelegramSession;
class MediaCache;
class QTimer;

class ChatsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY countChanged)
    Q_PROPERTY(int unreadTotal READ unreadTotal NOTIFY countChanged)
public:
    enum Roles {
        PeerKeyRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        TimeTextRole,
        UnreadRole,
        MutedRole,
        PinnedRole,
        GroupRole,
        OnlineRole,
        TypingRole,
        InitialsRole,
        ColorRole,
        AvatarRole      // file:// url of the profile picture, or "" (fall back to initials)
    };

    explicit ChatsModel(TelegramSession *session, MediaCache *media, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool hasMore() const;
    bool loading() const;
    int unreadTotal() const;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &peerKey) const;
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setMuted(const QString &peerKey, bool muted);
    Q_INVOKABLE void clearHistory(const QString &peerKey);

    /// Initials and a stable colour for the avatar circle.
    static QString initials(const QString &title);
    static QString colorFor(const TgPeer &peer);
    /// "12:30", "Yesterday", "Mon", "3 Sep" - the time column of the list.
    static QString timeText(int unixTime);

signals:
    void countChanged();

private slots:
    void onDialogsChanged();
    void onDialogChanged(const TgPeer &peer);
    void onPeerChanged(const TgPeer &peer);
    void onTyping(const TgPeer &peer, qint64 userId);
    void onTypingTimer();
    void onAvatarReady(const QString &key, const QString &path);

private:
    void refreshRow(const TgPeer &peer);

    TelegramSession *m_session;
    MediaCache *m_media;
    QHash<QString, int> m_typingUntil;      // peer key -> unix time the "typing" hint ends
    QHash<QString, qint64> m_typingWho;
    QTimer *m_typingTimer;
};

#endif // CHATSMODEL_H
