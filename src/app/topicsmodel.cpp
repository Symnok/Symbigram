// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "topicsmodel.h"

#include "telegramsession.h"

#include <QColor>

TopicsModel::TopicsModel(TelegramSession *session, QObject *parent)
    : QAbstractListModel(parent), m_session(session), m_loading(false), m_loaded(false)
{
    QHash<int, QByteArray> roles;
    roles[TopicIdRole] = "topicId";
    roles[TitleRole] = "title";
    roles[UnreadRole] = "unread";
    roles[ColorRole] = "color";
    roles[ClosedRole] = "closed";
    roles[PinnedRole] = "pinned";
    setRoleNames(roles);
    connect(m_session, SIGNAL(forumTopicsLoaded(TgPeer,QList<TgForumTopic>)),
            this, SLOT(onTopicsLoaded(TgPeer,QList<TgForumTopic>)));
}

int TopicsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_topics.size();
}

QVariant TopicsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_topics.size()) return QVariant();
    const TgForumTopic &t = m_topics.at(index.row());
    switch (role) {
    case TopicIdRole: return t.id;
    case TitleRole: return t.title;
    case UnreadRole: return t.unreadCount;
    case ColorRole: return colorHex(t.iconColor);
    case ClosedRole: return t.closed;
    case PinnedRole: return t.pinned;
    }
    return QVariant();
}

QString TopicsModel::title() const
{
    return m_peer.isNull() ? QString() : m_session->peers().title(m_peer);
}

void TopicsModel::loadFor(const QString &peerKey)
{
    TgPeer p = m_session->peers().withHash(TgPeer::fromKey(peerKey));
    beginResetModel();
    m_peer = p;
    m_topics.clear();
    m_loading = true;
    m_loaded = false;
    m_error.clear();
    endResetModel();
    emit changed();
    m_session->loadForumTopics(m_peer);
}

void TopicsModel::refresh()
{
    if (!m_peer.isNull()) loadFor(m_peer.key());
}

void TopicsModel::onTopicsLoaded(const TgPeer &peer, const QList<TgForumTopic> &topics)
{
    if (peer != m_peer) return;
    beginResetModel();
    // Pinned topics first, otherwise newest activity (highest top_message) first.
    QList<TgForumTopic> pinned, rest;
    for (int i = 0; i < topics.size(); ++i)
        (topics.at(i).pinned ? pinned : rest).append(topics.at(i));
    for (int i = 0; i < rest.size(); ++i)
        for (int j = i + 1; j < rest.size(); ++j)
            if (rest.at(j).topMessage > rest.at(i).topMessage) rest.swap(i, j);
    m_topics = pinned + rest;
    m_loading = false;
    m_loaded = true;
    if (m_topics.isEmpty()) m_error = tr("No topics.");
    endResetModel();
    emit changed();
}

QString TopicsModel::colorHex(int rgb)
{
    if (rgb == 0) return QString::fromLatin1("#5b8fd0");   // a sensible default
    return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF).name();
}
