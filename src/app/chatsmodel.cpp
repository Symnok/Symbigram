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
    : QAbstractListModel(parent), m_session(session), m_media(media), m_folderSel(-1), m_folderList(0)
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
    connect(session, SIGNAL(archiveChanged()), this, SLOT(onArchiveChanged()));
    connect(session, SIGNAL(foldersChanged()), this, SLOT(onFoldersChanged()));

    m_typingTimer = new QTimer(this);
    m_typingTimer->setInterval(1000);
    connect(m_typingTimer, SIGNAL(timeout()), this, SLOT(onTypingTimer()));
    rebuild();
}

int ChatsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

bool ChatsModel::hasMore() const
{
    // The Archive paginates on the server; a custom folder filters the (paginated) main
    // list, so loading more of the main list brings more to filter.
    return m_folderSel == -2 ? m_session->archiveHasMore() : m_session->dialogsHaveMore();
}
bool ChatsModel::loading() const { return m_session->dialogsLoading(); }

int ChatsModel::unreadTotal() const
{
    int n = 0;
    const QList<TgDialog> &d = m_session->dialogs();
    for (int i = 0; i < d.size(); ++i) n += d.at(i).unreadCount;
    return n;
}

const QList<TgDialog> &ChatsModel::sourceList() const
{
    return m_folderSel == -2 ? m_session->archivedDialogs() : m_session->dialogs();
}

void ChatsModel::rebuild()
{
    beginResetModel();
    m_view.clear();
    const QList<TgDialog> &src = sourceList();
    if (m_folderSel >= 0 && m_folderSel < m_session->folders().size()) {
        const TgFolder &f = m_session->folders().at(m_folderSel);
        int nowSec = now();
        for (int i = 0; i < src.size(); ++i)
            if (f.contains(src.at(i), m_session->peers().info(src.at(i).peer), nowSec)) m_view.append(src.at(i));
    } else {
        m_view = src;
    }
    endResetModel();
    emit countChanged();
}

QStringList ChatsModel::folderNames() const
{
    QStringList names;
    names << tr("All chats");
    const QList<TgFolder> &f = m_session->folders();
    for (int i = 0; i < f.size(); ++i) names << f.at(i).title;
    names << tr("Archive");
    return names;
}

QString ChatsModel::folderName() const
{
    QStringList n = folderNames();
    return (m_folderList >= 0 && m_folderList < n.size()) ? n.at(m_folderList) : tr("All chats");
}

bool ChatsModel::archiveSelected() const { return m_folderSel == -2; }

void ChatsModel::selectFolder(int listIndex)
{
    QStringList n = folderNames();
    if (listIndex < 0 || listIndex >= n.size()) return;
    m_folderList = listIndex;
    if (listIndex == 0) m_folderSel = -1;                       // All chats
    else if (listIndex == n.size() - 1) { m_folderSel = -2; m_session->loadArchive(); }   // Archive
    else m_folderSel = listIndex - 1;                          // a custom folder
    rebuild();
    emit folderChanged();
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
    if (!index.isValid() || index.row() >= m_view.size()) return QVariant();
    const TgDialog &d = m_view.at(index.row());
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
    for (int i = 0; i < m_view.size(); ++i)
        if (m_view.at(i).peer.key() == peerKey) return i;
    return -1;
}

void ChatsModel::loadMore()
{
    if (m_folderSel == -2) m_session->loadMoreArchive();
    else m_session->loadMoreDialogs();
    emit countChanged();
}
void ChatsModel::refresh() { m_session->refreshDialogs(); m_session->loadFolders(); if (m_folderSel == -2) m_session->loadArchive(); emit countChanged(); }
void ChatsModel::setMuted(const QString &peerKey, bool muted) { m_session->setMuted(TgPeer::fromKey(peerKey), muted); }
void ChatsModel::clearHistory(const QString &peerKey) { m_session->deleteHistory(TgPeer::fromKey(peerKey)); }

void ChatsModel::onDialogsChanged()
{
    if (m_folderSel == -2) return;      // the main list changed; the Archive view is unaffected
    rebuild();
}

void ChatsModel::onArchiveChanged()
{
    if (m_folderSel == -2) rebuild();
    emit foldersChanged();              // the Archive entry may appear/disappear (count hint)
}

void ChatsModel::onFoldersChanged()
{
    emit foldersChanged();
    // If the selected custom folder vanished, fall back to All chats.
    if (m_folderSel >= 0 && m_folderSel >= m_session->folders().size()) selectFolder(0);
    else rebuild();
}

void ChatsModel::refreshRow(const TgPeer &peer)
{
    int i = indexOf(peer.key());
    if (i >= 0) emit dataChanged(index(i), index(i));
}

void ChatsModel::onDialogChanged(const TgPeer &peer)
{
    int i = indexOf(peer.key());
    if (i >= 0) {
        // Keep the cached view row in step (unread/mute/preview live in the dialog).
        const QList<TgDialog> &src = sourceList();
        int si = -1;
        for (int j = 0; j < src.size(); ++j) if (src.at(j).peer == peer) { si = j; break; }
        if (si >= 0) m_view[i] = src.at(si);
        emit dataChanged(index(i), index(i));
    } else {
        // It may now match (or no longer match) the current folder.
        rebuild();
    }
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
