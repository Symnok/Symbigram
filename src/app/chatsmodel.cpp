// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "chatsmodel.h"
#include "mediacache.h"
#include "telegramsession.h"

#include <QDateTime>
#include <QUrl>
#include <QStringList>
#include <QTimer>

namespace
{
    const int TypingHintSec = 6;
    int now() { return int(QDateTime::currentDateTime().toTime_t()); }
}

ChatsModel::ChatsModel(TelegramSession *session, MediaCache *media, QObject *parent)
    : QAbstractListModel(parent), m_session(session), m_media(media)
{
    QHash<int, QByteArray> roles;
    roles[PeerKeyRole] = "peerKey";
    roles[TitleRole] = "title";
    roles[SubtitleRole] = "subtitle";
    roles[TimeTextRole] = "timeText";
    roles[UnreadRole] = "unread";
    roles[MutedRole] = "muted";
    roles[PinnedRole] = "pinned";
    roles[GroupRole] = "isGroup";
    roles[OnlineRole] = "online";
    roles[TypingRole] = "typing";
    roles[InitialsRole] = "initials";
    roles[ColorRole] = "color";
    roles[AvatarRole] = "avatar";
    setRoleNames(roles);

    connect(session, SIGNAL(dialogsChanged()), this, SLOT(onDialogsChanged()));
    connect(session, SIGNAL(dialogChanged(TgPeer)), this, SLOT(onDialogChanged(TgPeer)));
    connect(session, SIGNAL(peerChanged(TgPeer)), this, SLOT(onPeerChanged(TgPeer)));
    connect(session, SIGNAL(typing(TgPeer,qint64)), this, SLOT(onTyping(TgPeer,qint64)));
    connect(session, SIGNAL(stateChanged()), this, SIGNAL(countChanged()));
    connect(media, SIGNAL(ready(QString,QString)), this, SLOT(onAvatarReady(QString,QString)));

    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(1000);
    connect(m_typingTimer, SIGNAL(timeout()), this, SLOT(onTypingTimer()));
}

int ChatsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_session->dialogs().size();
}

bool ChatsModel::hasMore() const { return m_session->dialogsHaveMore(); }
bool ChatsModel::loading() const { return m_session->dialogsLoading(); }

int ChatsModel::unreadTotal() const
{
    int n = 0;
    const QList<TgDialog> &d = m_session->dialogs();
    for (int i = 0; i < d.size(); ++i) n += d.at(i).unreadCount;
    return n;
}

QString ChatsModel::initials(const QString &title)
{
    QStringList words = title.simplified().split(QLatin1Char(' '), QString::SkipEmptyParts);
    if (words.isEmpty()) return QLatin1String("?");
    if (words.size() == 1) return words.first().left(1).toUpper();
    return (words.first().left(1) + words.last().left(1)).toUpper();
}

QString ChatsModel::colorFor(const TgPeer &peer)
{
    static const char *const colors[] = { "#e17076", "#7bc862", "#e5ca77", "#65aadd", "#a695e7", "#ee7aae", "#6ec9cb", "#faa774" };
    return QLatin1String(colors[quint64(peer.id) % 8]);
}

QString ChatsModel::timeText(int unixTime)
{
    if (unixTime <= 0) return QString();
    QDateTime t = QDateTime::fromTime_t(unixTime);
    QDate today = QDate::currentDate();
    if (t.date() == today) return t.toString(QLatin1String("HH:mm"));
    if (t.date() == today.addDays(-1)) return tr("Yesterday");
    if (t.date() > today.addDays(-7)) return t.toString(QLatin1String("ddd"));
    if (t.date().year() == today.year()) return t.toString(QLatin1String("d MMM"));
    return t.toString(QLatin1String("d MMM yyyy"));
}

QVariant ChatsModel::data(const QModelIndex &index, int role) const
{
    const QList<TgDialog> &dialogs = m_session->dialogs();
    if (!index.isValid() || index.row() >= dialogs.size()) return QVariant();
    const TgDialog &d = dialogs.at(index.row());
    const TgPeerInfo info = m_session->peers().info(d.peer);
    const QString key = d.peer.key();
    switch (role) {
    case PeerKeyRole: return key;
    case TitleRole: return m_session->peers().title(d.peer);
    case SubtitleRole: {
        if (m_typingUntil.value(key, 0) > now()) {
            if (d.peer.isGroup()) return tr("%1 is typing...").arg(m_session->peers().userName(m_typingWho.value(key)));
            return tr("typing...");
        }
        QString text = d.lastText.simplified();
        if (text.isEmpty()) return QString();
        if (d.lastOut) return tr("You: %1").arg(text);
        if (d.peer.isGroup() && d.lastFromId && !info.isBroadcast)
            return m_session->peers().userName(d.lastFromId).section(QLatin1Char(' '), 0, 0) + QLatin1String(": ") + text;
        return text;
    }
    case TimeTextRole: return timeText(d.topMessageDate);
    case UnreadRole: return d.unreadCount;
    case MutedRole: return d.isMuted(now());
    case PinnedRole: return d.pinned;
    case GroupRole: return d.peer.isGroup();
    case OnlineRole: return d.peer.kind == TgPeer::User && info.online;
    case TypingRole: return m_typingUntil.value(key, 0) > now();
    case InitialsRole: return initials(m_session->peers().title(d.peer));
    case ColorRole: return colorFor(d.peer);
    case AvatarRole: {
        if (info.photoId == 0) return QString();
        QString path = m_media->peerPhoto(d.peer, info);
        return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
    }
    default: return QVariant();
    }
}

QVariantMap ChatsModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= rowCount()) return m;
    QModelIndex idx = index(row);
    QHash<int, QByteArray> names = roleNames();
    for (QHash<int, QByteArray>::const_iterator it = names.begin(); it != names.end(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    return m;
}

int ChatsModel::indexOf(const QString &peerKey) const
{
    const QList<TgDialog> &dialogs = m_session->dialogs();
    for (int i = 0; i < dialogs.size(); ++i)
        if (dialogs.at(i).peer.key() == peerKey) return i;
    return -1;
}

void ChatsModel::loadMore() { m_session->loadMoreDialogs(); emit countChanged(); }
void ChatsModel::refresh() { m_session->refreshDialogs(); emit countChanged(); }
void ChatsModel::setMuted(const QString &peerKey, bool muted) { m_session->setMuted(TgPeer::fromKey(peerKey), muted); }
void ChatsModel::clearHistory(const QString &peerKey) { m_session->deleteHistory(TgPeer::fromKey(peerKey)); }

void ChatsModel::onDialogsChanged()
{
    // Order and membership may both have changed: a full reset is the honest answer, and
    // the list is short enough that the view redraws in a moment.
    beginResetModel();
    endResetModel();
    emit countChanged();
}

void ChatsModel::refreshRow(const TgPeer &peer)
{
    int i = indexOf(peer.key());
    if (i >= 0) emit dataChanged(index(i), index(i));
}

void ChatsModel::onDialogChanged(const TgPeer &peer)
{
    refreshRow(peer);
    emit countChanged();
}

void ChatsModel::onPeerChanged(const TgPeer &peer)
{
    refreshRow(peer);
}

void ChatsModel::onTyping(const TgPeer &peer, qint64 userId)
{
    m_typingUntil.insert(peer.key(), now() + TypingHintSec);
    m_typingWho.insert(peer.key(), userId);
    refreshRow(peer);
    if (!m_typingTimer->isActive()) m_typingTimer->start();
}

void ChatsModel::onAvatarReady(const QString &key, const QString &path)
{
    Q_UNUSED(path);
    if (!key.startsWith(QLatin1Char('p'))) return;   // a peer photo (media keys start with 'm')
    // The list is short; a blanket refresh is cheaper than mapping the photo id to a row.
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void ChatsModel::onTypingTimer()
{
    bool any = false;
    QList<QString> keys = m_typingUntil.keys();
    for (int i = 0; i < keys.size(); ++i) {
        if (m_typingUntil.value(keys.at(i)) > now()) { any = true; continue; }
        m_typingUntil.remove(keys.at(i));
        refreshRow(TgPeer::fromKey(keys.at(i)));
    }
    if (!any) m_typingTimer->stop();
}
