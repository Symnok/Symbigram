// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "messagesmodel.h"
#include "chatsmodel.h"
#include "telegramsession.h"

#include <QDateTime>
#include <QTime>
#include <QTimer>

namespace
{
    const int HistoryPage = 30;
    const int TypingIdleMs = 5000;
    const int PeerTypingMs = 6000;
    int now() { return int(QDateTime::currentDateTime().toTime_t()); }
}

MessagesModel::MessagesModel(TelegramSession *session, QObject *parent)
    : QAbstractListModel(parent), m_session(session), m_loading(false), m_hasOlder(false), m_readOutboxMaxId(0),
      m_typingSent(false), m_peerTypingUser(0)
{
    QHash<int, QByteArray> roles;
    roles[MsgIdRole] = "msgId";
    roles[BodyRole] = "body";
    roles[NoteRole] = "note";
    roles[OutRole] = "out";
    roles[SenderRole] = "sender";
    roles[TimeTextRole] = "timeText";
    roles[DateTextRole] = "dateText";
    roles[ShowDateRole] = "showDate";
    roles[PendingRole] = "pending";
    roles[FailedRole] = "failed";
    roles[ReadRole] = "read";
    roles[ServiceRole] = "service";
    roles[ForwardedRole] = "forwarded";
    roles[ReplyRole] = "reply";
    roles[EditedRole] = "edited";
    setRoleNames(roles);

    connect(session, SIGNAL(historyLoaded(TgPeer,QList<TgMessage>,int,bool)), this, SLOT(onHistoryLoaded(TgPeer,QList<TgMessage>,int,bool)));
    connect(session, SIGNAL(historyFailed(TgPeer,QString)), this, SLOT(onHistoryFailed(TgPeer,QString)));
    connect(session, SIGNAL(messageReceived(TgMessage)), this, SLOT(onMessageReceived(TgMessage)));
    connect(session, SIGNAL(messageEdited(TgMessage)), this, SLOT(onMessageEdited(TgMessage)));
    connect(session, SIGNAL(messagesDeleted(TgPeer,QList<int>)), this, SLOT(onMessagesDeleted(TgPeer,QList<int>)));
    connect(session, SIGNAL(messageSent(TgPeer,qint64,int,int)), this, SLOT(onMessageSent(TgPeer,qint64,int,int)));
    connect(session, SIGNAL(messageFailed(TgPeer,qint64,QString)), this, SLOT(onMessageFailed(TgPeer,qint64,QString)));
    connect(session, SIGNAL(typing(TgPeer,qint64)), this, SLOT(onTyping(TgPeer,qint64)));
    connect(session, SIGNAL(peerChanged(TgPeer)), this, SLOT(onPeerChanged(TgPeer)));
    connect(session, SIGNAL(dialogChanged(TgPeer)), this, SLOT(onPeerChanged(TgPeer)));
    connect(session, SIGNAL(readOutbox(TgPeer,int)), this, SLOT(onReadOutbox(TgPeer,int)));
    connect(session, SIGNAL(stateChanged()), this, SIGNAL(peerChanged()));

    m_typingTimer = new QTimer(this);
    m_typingTimer->setSingleShot(true);
    m_typingTimer->setInterval(TypingIdleMs);
    connect(m_typingTimer, SIGNAL(timeout()), this, SLOT(onTypingIdle()));
    m_peerTypingTimer = new QTimer(this);
    m_peerTypingTimer->setSingleShot(true);
    m_peerTypingTimer->setInterval(PeerTypingMs);
    connect(m_peerTypingTimer, SIGNAL(timeout()), this, SLOT(onPeerTypingIdle()));
}

int MessagesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QString MessagesModel::timeText(int unixTime)
{
    return QDateTime::fromTime_t(unixTime).toString(QLatin1String("HH:mm"));
}

QString MessagesModel::dateText(int unixTime)
{
    QDate d = QDateTime::fromTime_t(unixTime).date();
    QDate today = QDate::currentDate();
    if (d == today) return tr("Today");
    if (d == today.addDays(-1)) return tr("Yesterday");
    return d.toString(QLatin1String("d MMMM yyyy"));
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return QVariant();
    const Row &r = m_rows.at(index.row());
    const TgMessage &m = r.m;
    switch (role) {
    case MsgIdRole: return m.id;
    case BodyRole: return m.text;
    case NoteRole: return m.note;
    case OutRole: return m.out;
    case SenderRole: return (m.out || !m_peer.isGroup() || m.service) ? QString() : m_session->peers().userName(m.fromId);
    case TimeTextRole: return timeText(m.date);
    case DateTextRole: return dateText(m.date);
    case ShowDateRole: return index.row() == 0
        || QDateTime::fromTime_t(m_rows.at(index.row() - 1).m.date).date() != QDateTime::fromTime_t(m.date).date();
    case PendingRole: return r.pending;
    case FailedRole: return r.failed;
    case ReadRole: return m.out && !r.pending && m.id > 0 && m.id <= m_readOutboxMaxId;
    case ServiceRole: return m.service;
    case ForwardedRole: {
        if (m.forwardedFrom.isEmpty()) return QString();
        if (m.forwardedFrom.contains(QLatin1Char(':'))) return m_session->peers().title(TgPeer::fromKey(m.forwardedFrom));
        return m.forwardedFrom;
    }
    case ReplyRole: {
        if (!m.replyToId) return QString();
        int row = rowById(m.replyToId);
        if (row < 0) return tr("reply");
        const TgMessage &to = m_rows.at(row).m;
        QString who = to.out ? tr("You") : m_session->peers().userName(to.fromId);
        QString text = to.text.isEmpty() ? to.note : to.text;
        return who + QLatin1String(": ") + text.simplified().left(60);
    }
    case EditedRole: return m.editDate > 0;
    default: return QVariant();
    }
}

QVariantMap MessagesModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_rows.size()) return m;
    QModelIndex idx = index(row);
    QHash<int, QByteArray> names = roleNames();
    for (QHash<int, QByteArray>::const_iterator it = names.begin(); it != names.end(); ++it)
        m.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
    return m;
}

int MessagesModel::rowById(int id) const
{
    if (id <= 0) return -1;
    for (int i = m_rows.size() - 1; i >= 0; --i)
        if (m_rows.at(i).m.id == id) return i;
    return -1;
}

int MessagesModel::rowByRandomId(qint64 randomId) const
{
    for (int i = m_rows.size() - 1; i >= 0; --i)
        if (m_rows.at(i).randomId == randomId) return i;
    return -1;
}

// -- peer ---------------------------------------------------------------------------------------

QString MessagesModel::title() const
{
    return m_peer.isNull() ? QString() : m_session->peers().title(m_peer);
}

bool MessagesModel::peerIsChannel() const
{
    return m_peer.kind == TgPeer::Channel && m_session->peers().info(m_peer).isBroadcast;
}

bool MessagesModel::peerMuted() const
{
    return m_session->dialog(m_peer).isMuted(now());
}

QString MessagesModel::initials() const { return ChatsModel::initials(title()); }
QString MessagesModel::color() const { return ChatsModel::colorFor(m_peer); }

bool MessagesModel::peerTyping() const { return m_peerTypingTimer->isActive(); }

QString MessagesModel::lastSeenText(const TgPeerInfo &info) const
{
    switch (info.statusKind) {
    case 1: return tr("online");
    case 2: {
        QDateTime t = QDateTime::fromTime_t(info.lastSeen);
        QDate today = QDate::currentDate();
        if (t.date() == today) return tr("last seen at %1").arg(t.toString(QLatin1String("HH:mm")));
        if (t.date() == today.addDays(-1)) return tr("last seen yesterday at %1").arg(t.toString(QLatin1String("HH:mm")));
        return tr("last seen %1").arg(t.toString(QLatin1String("d MMM")));
    }
    case 3: return tr("last seen recently");
    case 4: return tr("last seen within a week");
    case 5: return tr("last seen within a month");
    case 6: return tr("last seen a long time ago");
    default: return QString();
    }
}

QString MessagesModel::subtitle() const
{
    if (m_peer.isNull()) return QString();
    if (m_peerTypingTimer->isActive()) {
        if (m_peer.isGroup()) return tr("%1 is typing...").arg(m_session->peers().userName(m_peerTypingUser));
        return tr("typing...");
    }
    TgPeerInfo info = m_session->peers().info(m_peer);
    if (m_peer.kind == TgPeer::User) {
        if (info.isBot) return tr("bot");
        if (info.isSelf) return tr("Saved Messages");
        return lastSeenText(info);
    }
    if (info.isBroadcast) return info.membersCount > 0 ? tr("channel, %1 subscribers").arg(info.membersCount) : tr("channel");
    return info.membersCount > 0 ? tr("%1 members").arg(info.membersCount) : tr("group");
}

void MessagesModel::onPeerChanged(const TgPeer &peer)
{
    if (peer == m_peer) emit peerChanged();
}

void MessagesModel::onTyping(const TgPeer &peer, qint64 userId)
{
    if (peer != m_peer || userId == m_session->selfId()) return;
    m_peerTypingUser = userId;
    m_peerTypingTimer->start();
    emit peerChanged();
}

void MessagesModel::onPeerTypingIdle() { emit peerChanged(); }

// -- open/close ------------------------------------------------------------------------------------

void MessagesModel::open(const QString &peerKey)
{
    TgPeer p = TgPeer::fromKey(peerKey);
    if (p == m_peer && !m_rows.isEmpty()) return;
    close();
    beginResetModel();
    m_peer = m_session->peers().withHash(p);
    m_rows.clear();
    m_readOutboxMaxId = m_session->dialog(m_peer).readOutboxMaxId;
    endResetModel();
    m_error.clear();
    m_loading = true;
    m_hasOlder = false;
    emit chatChanged();
    emit peerChanged();
    emit countChanged();
    emit loadingChanged();
    m_session->loadHistory(m_peer, 0, HistoryPage);
}

void MessagesModel::close()
{
    if (m_peer.isNull()) return;
    if (m_typingSent) { m_session->setTyping(m_peer, false); m_typingSent = false; }
    m_typingTimer->stop();
    m_peerTypingTimer->stop();
    beginResetModel();
    m_peer = TgPeer();
    m_rows.clear();
    endResetModel();
    m_loading = false;
    emit chatChanged();
    emit peerChanged();
    emit countChanged();
    emit loadingChanged();
}

void MessagesModel::loadOlder()
{
    if (m_peer.isNull() || m_loading || !m_hasOlder || m_rows.isEmpty()) return;
    int oldest = 0;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).m.id > 0) { oldest = m_rows.at(i).m.id; break; }
    if (!oldest) return;
    m_loading = true;
    emit loadingChanged();
    m_session->loadHistory(m_peer, oldest, HistoryPage);
}

void MessagesModel::onHistoryLoaded(const TgPeer &peer, const QList<TgMessage> &messages, int offsetId, bool more)
{
    if (peer != m_peer) return;
    m_loading = false;
    m_hasOlder = more;
    // The server sends newest first; the model keeps oldest first. New rows go before
    // whatever is already there (an older page) or replace the empty start.
    QList<Row> rows;
    for (int i = messages.size() - 1; i >= 0; --i) {
        if (rowById(messages.at(i).id) >= 0) continue;
        Row r;
        r.m = messages.at(i);
        rows.append(r);
    }
    if (!rows.isEmpty()) {
        beginInsertRows(QModelIndex(), 0, rows.size() - 1);
        for (int i = rows.size() - 1; i >= 0; --i) m_rows.prepend(rows.at(i));
        endInsertRows();
        emit countChanged();
        if (offsetId == 0) emit messageAppended();
        else emit olderPrepended(rows.size());
    }
    emit loadingChanged();
    if (offsetId == 0) markRead();
}

void MessagesModel::onHistoryFailed(const TgPeer &peer, const QString &error)
{
    if (peer != m_peer) return;
    m_loading = false;
    m_error = error;
    emit loadingChanged();
}

void MessagesModel::markRead()
{
    if (m_peer.isNull()) return;
    int maxId = 0;
    for (int i = m_rows.size() - 1; i >= 0; --i)
        if (m_rows.at(i).m.id > 0) { maxId = m_rows.at(i).m.id; break; }
    TgDialog d = m_session->dialog(m_peer);
    if (maxId > 0 && (d.unreadCount > 0 || maxId > d.readInboxMaxId)) m_session->markRead(m_peer, maxId);
}

// -- incoming --------------------------------------------------------------------------------------

void MessagesModel::onMessageReceived(const TgMessage &m)
{
    if (m.peer != m_peer) return;
    if (rowById(m.id) >= 0) return;
    // Our own send echoed back through updates while its result is pending: tie it to
    // the pending row by text rather than showing it twice.
    if (m.out) {
        for (int i = m_rows.size() - 1; i >= 0; --i) {
            if (m_rows.at(i).pending && m_rows.at(i).m.text == m.text) {
                m_rows[i].m = m;
                m_rows[i].pending = false;
                emit dataChanged(index(i), index(i));
                return;
            }
        }
    }
    Row r;
    r.m = m;
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
    emit messageAppended();
    if (!m.out) m_peerTypingTimer->stop();
}

void MessagesModel::onMessageEdited(const TgMessage &m)
{
    if (m.peer != m_peer) return;
    int row = rowById(m.id);
    if (row < 0) return;
    m_rows[row].m = m;
    emit dataChanged(index(row), index(row));
}

void MessagesModel::onMessagesDeleted(const TgPeer &peer, const QList<int> &ids)
{
    if (!peer.isNull() && peer != m_peer) return;
    for (int i = 0; i < ids.size(); ++i) {
        int row = rowById(ids.at(i));
        if (row < 0) continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }
    emit countChanged();
}

void MessagesModel::onReadOutbox(const TgPeer &peer, int maxId)
{
    if (peer != m_peer) return;
    m_readOutboxMaxId = qMax(m_readOutboxMaxId, maxId);
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(m_rows.size() - 1));
}

// -- sending ------------------------------------------------------------------------------------------

void MessagesModel::send(const QString &text)
{
    QString t = text.trimmed();
    if (m_peer.isNull() || t.isEmpty()) return;
    if (m_typingSent) { m_typingSent = false; m_typingTimer->stop(); }
    Row r;
    r.m.peer = m_peer;
    r.m.text = t;
    r.m.out = true;
    r.m.date = now();
    r.m.fromId = m_session->selfId();
    r.pending = true;
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
    emit messageAppended();
    sendRow(m_rows.size() - 1);
}

void MessagesModel::sendRow(int row)
{
    if (!m_session->isOnline()) {
        m_rows[row].pending = false;
        m_rows[row].failed = true;
        emit dataChanged(index(row), index(row));
        emit sendFailed(tr("Not connected."));
        return;
    }
    m_rows[row].randomId = m_session->sendText(m_peer, m_rows.at(row).m.text);
    m_rows[row].pending = true;
    m_rows[row].failed = false;
    emit dataChanged(index(row), index(row));
}

void MessagesModel::retry(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).failed) return;
    m_rows[row].m.date = now();
    sendRow(row);
}

void MessagesModel::onMessageSent(const TgPeer &peer, qint64 randomId, int msgId, int date)
{
    if (peer != m_peer) return;
    int row = rowByRandomId(randomId);
    if (row < 0) return;
    m_rows[row].pending = false;
    m_rows[row].failed = false;
    m_rows[row].m.id = msgId;
    if (date) m_rows[row].m.date = date;
    emit dataChanged(index(row), index(row));
}

void MessagesModel::onMessageFailed(const TgPeer &peer, qint64 randomId, const QString &error)
{
    if (peer != m_peer) return;
    int row = rowByRandomId(randomId);
    if (row < 0) return;
    m_rows[row].pending = false;
    m_rows[row].failed = true;
    emit dataChanged(index(row), index(row));
    emit sendFailed(tr("The message was not sent: %1").arg(error));
}

void MessagesModel::composing(const QString &text)
{
    if (m_peer.isNull() || peerIsChannel()) return;
    if (text.isEmpty()) {
        if (m_typingSent) { m_session->setTyping(m_peer, false); m_typingSent = false; }
        m_typingTimer->stop();
        return;
    }
    // Telegram shows "typing" for about six seconds per notification, so it is repeated
    // while the text keeps changing.
    if (!m_typingSent || m_typingSentAt.elapsed() > 4000) {
        m_session->setTyping(m_peer, true);
        m_typingSent = true;
        m_typingSentAt.restart();
    }
    m_typingTimer->start();
}

void MessagesModel::onTypingIdle()
{
    if (m_typingSent) { m_session->setTyping(m_peer, false); m_typingSent = false; }
}

void MessagesModel::setMuted(bool muted)
{
    if (!m_peer.isNull()) m_session->setMuted(m_peer, muted);
}

void MessagesModel::deleteMessage(int row, bool forEveryone)
{
    if (row < 0 || row >= m_rows.size()) return;
    int id = m_rows.at(row).m.id;
    if (id > 0) m_session->deleteMessages(m_peer, QList<int>() << id, forEveryone);
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
}
