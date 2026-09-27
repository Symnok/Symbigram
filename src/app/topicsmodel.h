// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The topic list of one forum (supergroup with topics) for QML. loadFor(peerKey) fetches the
// topics; tapping one opens it in the MessagesModel (openTopic). Pinned topics come first.
#ifndef TOPICSMODEL_H
#define TOPICSMODEL_H

#include "tgtypes.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>

class TelegramSession;

class TopicsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString peerKey READ peerKey NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)      // the group's name (page heading)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum Roles {
        TopicIdRole = Qt::UserRole + 1,
        TitleRole,
        UnreadRole,
        ColorRole,       // "#rrggbb" for the letter icon
        ClosedRole,
        PinnedRole
    };

    explicit TopicsModel(TelegramSession *session, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    QString peerKey() const { return m_peer.isNull() ? QString() : m_peer.key(); }
    QString title() const;
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }

    Q_INVOKABLE void loadFor(const QString &peerKey);
    Q_INVOKABLE void refresh();

signals:
    void changed();

private slots:
    void onTopicsLoaded(const TgPeer &peer, const QList<TgForumTopic> &topics);

private:
    static QString colorHex(int rgb);

    TelegramSession *m_session;
    TgPeer m_peer;
    QList<TgForumTopic> m_topics;
    bool m_loading;
    bool m_loaded;         // a reply has arrived (so an empty list reads as "no topics", not "loading")
    QString m_error;
};

#endif // TOPICSMODEL_H
