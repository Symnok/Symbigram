// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "messagesmodel.h"
#include "chatsmodel.h"
#include "mediacache.h"
#include "telegramsession.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>
#include <QTimer>
#include <QUrl>

namespace
{
    const int HistoryPage = 30;
    const int TypingIdleMs = 5000;
    const int PeerTypingMs = 6000;
    int now() { return int(QDateTime::currentDateTime().toTime_t()); }
}

MessagesModel::MessagesModel(TelegramSession *session, MediaCache *media, QObject *parent)
    : QAbstractListModel(parent), m_session(session), m_media(media), m_loading(false), m_hasOlder(false), m_readOutboxMaxId(0),
      m_typingSent(false), m_peerTypingUser(0), m_secretId(0)
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
    roles[MediaKindRole] = "mediaKind";
    roles[MediaThumbRole] = "mediaThumb";
    roles[MediaStateRole] = "mediaState";
    roles[MediaProgressRole] = "mediaProgress";
    roles[MediaInfoRole] = "mediaInfo";
    roles[MediaWidthRole] = "mediaWidth";
    roles[MediaHeightRole] = "mediaHeight";
    roles[LocalPathRole] = "localPath";
    setRoleNames(roles);
    connect(media, SIGNAL(ready(QString,QString)), this, SLOT(onMediaReady(QString,QString)));
    connect(media, SIGNAL(failed(QString,QString)), this, SLOT(onMediaFailed(QString,QString)));
    connect(media, SIGNAL(progress(QString,int)), this, SLOT(onMediaProgress(QString,int)));

    connect(session, SIGNAL(historyLoaded(TgPeer,QList<TgMessage>,int,bool)), this, SLOT(onHistoryLoaded(TgPeer,QList<TgMessage>,int,bool)));
    connect(session, SIGNAL(historyFailed(TgPeer,QString)), this, SLOT(onHistoryFailed(TgPeer,QString)));
    connect(session, SIGNAL(messageReceived(TgMessage)), this, SLOT(onMessageReceived(TgMessage)));
    connect(session, SIGNAL(messageEdited(TgMessage)), this, SLOT(onMessageEdited(TgMessage)));
    connect(session, SIGNAL(messagesDeleted(TgPeer,QList<int>)), this, SLOT(onMessagesDeleted(TgPeer,QList<int>)));
    connect(session, SIGNAL(messageSent(TgPeer,qint64,TgMessage)), this, SLOT(onMessageSent(TgPeer,qint64,TgMessage)));
    connect(session, SIGNAL(messageFailed(TgPeer,qint64,QString)), this, SLOT(onMessageFailed(TgPeer,qint64,QString)));
    connect(session, SIGNAL(typing(TgPeer,qint64)), this, SLOT(onTyping(TgPeer,qint64)));
    connect(session, SIGNAL(peerChanged(TgPeer)), this, SLOT(onPeerChanged(TgPeer)));
    connect(session, SIGNAL(dialogChanged(TgPeer)), this, SLOT(onPeerChanged(TgPeer)));
    connect(session, SIGNAL(readOutbox(TgPeer,int)), this, SLOT(onReadOutbox(TgPeer,int)));
    connect(session, SIGNAL(stateChanged()), this, SIGNAL(peerChanged()));
    connect(session, SIGNAL(secretMessageReceived(int,qint64,QString,int,bool)), this, SLOT(onSecretMessage(int,qint64,QString,int,bool)));
    connect(session, SIGNAL(secretChatsChanged()), this, SLOT(onSecretChatsChanged()));

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
    case MediaKindRole: return mediaKindName(m.media.kind);
    case MediaThumbRole: return r.thumbPath.isEmpty() ? QString() : QUrl::fromLocalFile(r.thumbPath).toString();
    case MediaStateRole:
        if (!m.media.isValid()) return QLatin1String("none");
        if (r.mediaFailed) return QLatin1String("failed");
        if (r.mediaLoading) return QLatin1String("loading");
        if (!r.fullPath.isEmpty()) return QLatin1String("ready");
        return QLatin1String("idle");
    case MediaProgressRole: return r.progress;
    case MediaInfoRole: return mediaInfoText(m.media);
    case MediaWidthRole: return m.media.width;
    case MediaHeightRole: return m.media.height;
    case LocalPathRole: return r.fullPath;
    default: return QVariant();
    }
}

QString MessagesModel::mediaKindName(TgMedia::Kind k)
{
    switch (k) {
    case TgMedia::Photo: return QLatin1String("photo");
    case TgMedia::Video: return QLatin1String("video");
    case TgMedia::Voice: return QLatin1String("voice");
    case TgMedia::Audio: return QLatin1String("audio");
    case TgMedia::Sticker: return QLatin1String("sticker");
    case TgMedia::Gif: return QLatin1String("gif");
    case TgMedia::Document: return QLatin1String("document");
    default: return QString();
    }
}

QString MessagesModel::mediaInfoText(const TgMedia &m) const
{
    QString size;
    qint64 b = m.fileSize;
    if (b >= 1048576) size = tr("%1 MB").arg(double(b) / 1048576.0, 0, 'f', 1);
    else if (b >= 1024) size = tr("%1 KB").arg(int(b / 1024));
    else if (b > 0) size = tr("%1 B").arg(b);
    QString dur;
    if (m.duration > 0) dur = QString::fromLatin1("%1:%2").arg(m.duration / 60).arg(m.duration % 60, 2, 10, QLatin1Char('0'));
    if (m.kind == TgMedia::Voice || m.kind == TgMedia::Audio) return dur.isEmpty() ? size : dur;
    if (m.kind == TgMedia::Document) return m.fileName + (size.isEmpty() ? QString() : QLatin1String("  ") + size);
    if (m.kind == TgMedia::Video || m.kind == TgMedia::Gif) return dur.isEmpty() ? size : dur + QLatin1String("  ") + size;
    return size;
}

void MessagesModel::prepareMedia(Row &r)
{
    if (!r.m.media.isValid() || !m_media) return;
    TgMedia &m = r.m.media;
    if (m.kind == TgMedia::Photo || m.kind == TgMedia::Sticker) {
        // An instant blurred placeholder, then the small size fetched automatically.
        QString stripped = m_media->strippedThumbFile(m);
        if (!stripped.isEmpty()) r.thumbPath = stripped;
        QString cached = m_media->cachedFile(m, m.sizeType);
        if (!cached.isEmpty()) { r.thumbPath = cached; r.fullPath = cached; }
        else { r.awaitKey = m_media->fetch(m, m.sizeType); r.mediaLoading = true; }
    } else {
        // Videos/documents carry a thumbnail; show it if there is one, but do not fetch
        // the (large) file until asked.
        if (!m.thumbSizeType.isEmpty()) {
            QString cached = m_media->cachedFile(m, m.thumbSizeType);
            if (!cached.isEmpty()) r.thumbPath = cached;
            else m_media->fetch(m, m.thumbSizeType);
        }
        QString full = m_media->cachedFile(m, QString());
        if (!full.isEmpty()) r.fullPath = full;
    }
}

int MessagesModel::rowByKey(const QString &key) const
{
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        const TgMedia &m = m_rows.at(i).m.media;
        if (!m.isValid()) continue;
        if (m_rows.at(i).awaitKey == key) return i;
        if (m_media && (m_media->keyFor(m, m.sizeType) == key || m_media->keyFor(m, QString()) == key || m_media->keyFor(m, m.thumbSizeType) == key)) return i;
    }
    return -1;
}

void MessagesModel::downloadMedia(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_media) return;
    Row &r = m_rows[row];
    TgMedia &m = r.m.media;
    if (!m.isValid()) return;
    QString size = (m.kind == TgMedia::Photo) ? m.bigSizeType : QString();
    QString cached = m_media->cachedFile(m, size);
    if (!cached.isEmpty()) { r.fullPath = cached; r.mediaLoading = false; r.mediaFailed = false; emit dataChanged(index(row), index(row)); return; }
    r.awaitKey = m_media->fetch(m, size);
    r.mediaLoading = true;
    r.mediaFailed = false;
    r.progress = 0;
    emit dataChanged(index(row), index(row));
}

void MessagesModel::saveMedia(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    Row &r = m_rows[row];
    if (r.fullPath.isEmpty()) { downloadMedia(row); return; }
    QString base = QDesktopServices::storageLocation(QDesktopServices::PicturesLocation);
    if (r.m.media.kind != TgMedia::Photo || base.isEmpty())
        base = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    if (base.isEmpty()) base = QDir::homePath();
    base += QLatin1String("/Symbigram");
    QDir().mkpath(base);
    QString name = r.m.media.fileName.isEmpty() ? QFileInfo(r.fullPath).fileName() : r.m.media.fileName;
    QString target = base + QLatin1Char('/') + name;
    for (int n = 1; QFile::exists(target); ++n) {
        QFileInfo fi(base + QLatin1Char('/') + name);
        target = QString::fromLatin1("%1/%2 (%3).%4").arg(base, fi.completeBaseName()).arg(n).arg(fi.suffix());
    }
    if (QFile::copy(r.fullPath, target)) emit sendFailed(tr("Saved to %1").arg(QDir::toNativeSeparators(target)));
    else emit sendFailed(tr("Could not save the file."));
}

void MessagesModel::openMedia(int row)
{
    if (row < 0 || row >= m_rows.size()) return;
    if (m_rows.at(row).fullPath.isEmpty()) { downloadMedia(row); return; }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_rows.at(row).fullPath));
}

bool MessagesModel::canDeleteForEveryone(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_secretId) return false;
    // One-to-one chats and basic groups let anyone revoke; a channel post needs rights we
    // do not track, so it is offered only for our own posts there.
    if (m_peer.kind == TgPeer::User) return true;
    if (m_peer.kind == TgPeer::Chat) return true;
    return m_rows.at(row).m.out;
}

void MessagesModel::onMediaReady(const QString &key, const QString &path)
{
    int row = rowByKey(key);
    if (row < 0) return;
    Row &r = m_rows[row];
    const TgMedia &m = r.m.media;
    bool isThumb = m_media && (m_media->keyFor(m, m.thumbSizeType) == key ||
                               ((m.kind == TgMedia::Photo || m.kind == TgMedia::Sticker) && m_media->keyFor(m, m.sizeType) == key));
    if (isThumb) {
        r.thumbPath = path;
        if (m.kind == TgMedia::Photo || m.kind == TgMedia::Sticker) { r.fullPath = path; r.mediaLoading = false; }
    } else {
        r.fullPath = path;
        r.thumbPath = (m.kind == TgMedia::Photo || m.kind == TgMedia::Sticker) ? path : r.thumbPath;
        r.mediaLoading = false;
    }
    r.awaitKey.clear();
    emit dataChanged(index(row), index(row));
}

void MessagesModel::onMediaFailed(const QString &key, const QString &error)
{
    Q_UNUSED(error);
    int row = rowByKey(key);
    if (row < 0) return;
    m_rows[row].mediaLoading = false;
    m_rows[row].mediaFailed = true;
    m_rows[row].awaitKey.clear();
    emit dataChanged(index(row), index(row));
}

void MessagesModel::onMediaProgress(const QString &key, int percent)
{
    int row = rowByKey(key);
    if (row < 0) return;
    m_rows[row].progress = percent;
    emit dataChanged(index(row), index(row));
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

QString MessagesModel::avatar() const
{
    if (m_peer.isNull() || !m_media) return QString();
    TgPeerInfo info = m_session->peers().info(m_peer);
    if (info.photoId == 0) return QString();
    QString path = m_media->peerPhoto(m_peer, info);
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

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
    if (m_secretId) {
        TgSecretChat sc = m_session->secretChat(m_secretId);
        if (sc.state == 1) return tr("wants to start a secret chat");
        if (sc.state == 0) return tr("waiting to be accepted...");
        if (sc.ttl > 0) return tr("end-to-end encrypted, self-destruct %1s").arg(sc.ttl);
        return tr("end-to-end encrypted");
    }
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
    if (peerKey.startsWith(QLatin1String("secret:"))) { openSecret(peerKey.mid(7).toInt()); return; }
    TgPeer p = TgPeer::fromKey(peerKey);
    if (p == m_peer && m_secretId == 0 && !m_rows.isEmpty()) return;
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

void MessagesModel::openSecret(int id)
{
    if (m_secretId == id && !m_rows.isEmpty()) return;
    close();
    TgSecretChat sc = m_session->secretChat(id);
    beginResetModel();
    m_secretId = id;
    // Borrow the peer for the header (name, avatar, presence).
    TgPeer u; u.kind = TgPeer::User; u.id = sc.peerUserId;
    m_peer = m_session->peers().withHash(u);
    m_rows.clear();
    QList<TgMessage> hist = m_session->secretHistory(id);
    for (int i = 0; i < hist.size(); ++i) { Row r; r.m = hist.at(i); m_rows.append(r); }
    endResetModel();
    m_error.clear();
    m_loading = false;
    m_hasOlder = false;
    emit chatChanged();
    emit peerChanged();
    emit countChanged();
    emit loadingChanged();
}

void MessagesModel::close()
{
    if (m_peer.isNull()) return;
    if (m_typingSent) { m_session->setTyping(m_peer, false); m_typingSent = false; }
    m_typingTimer->stop();
    m_peerTypingTimer->stop();
    beginResetModel();
    m_peer = TgPeer();
    m_secretId = 0;
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
        prepareMedia(r);
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
    if (m_peer.isNull() || m_secretId) return;
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
    prepareMedia(r);
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
    prepareMedia(m_rows[row]);
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
    if (t.isEmpty()) return;
    if (m_secretId) { m_session->sendSecretText(m_secretId, t); return; }  // the echo appends the row
    if (m_peer.isNull()) return;
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

void MessagesModel::onMessageSent(const TgPeer &peer, qint64 randomId, const TgMessage &message)
{
    if (peer != m_peer) return;
    int row = rowByRandomId(randomId);
    if (row < 0) {
        // A media send started elsewhere (the attach button) with no placeholder row.
        if (message.id <= 0 || rowById(message.id) >= 0) return;
        Row r;
        r.m = message;
        r.m.peer = m_peer;
        prepareMedia(r);
        beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
        m_rows.append(r);
        endInsertRows();
        emit countChanged();
        emit messageAppended();
        return;
    }
    Row &r = m_rows[row];
    r.pending = false;
    r.failed = false;
    r.m.id = message.id;
    if (message.date) r.m.date = message.date;
    // An uploaded attachment now has server media (id/reference); keep the local file as
    // its own thumbnail so it need not be downloaded again.
    if (message.media.isValid()) {
        QString localThumb = r.thumbPath;
        QString localFull = r.fullPath;
        r.m.media = message.media;
        r.m.note = message.note;
        r.thumbPath = localThumb;
        r.fullPath = localFull;
        if (r.thumbPath.isEmpty()) prepareMedia(r);
    }
    emit dataChanged(index(row), index(row));
}

void MessagesModel::noteOutgoingMedia(qint64 randomId, const QString &localPath, bool asPhoto)
{
    if (m_peer.isNull()) return;
    Row r;
    r.m.peer = m_peer;
    r.m.out = true;
    r.m.date = now();
    r.m.fromId = m_session->selfId();
    r.randomId = randomId;
    r.pending = true;
    // Show the file being sent straight away, from the local copy.
    r.m.media.kind = asPhoto ? TgMedia::Photo : TgMedia::Document;
    r.m.media.fileName = QFileInfo(localPath).fileName();
    if (asPhoto) r.thumbPath = localPath;
    else r.fullPath = localPath;
    r.m.note = asPhoto ? tr("photo") : r.m.media.fileName;
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
    emit messageAppended();
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

// -- secret (end-to-end) chats ---------------------------------------------------------------------

int MessagesModel::secretState() const
{
    return m_secretId ? m_session->secretChat(m_secretId).state : -1;
}

int MessagesModel::secretTtl() const
{
    return m_secretId ? m_session->secretChat(m_secretId).ttl : 0;
}

QString MessagesModel::secretKeyHex() const
{
    if (!m_secretId) return QString();
    QByteArray h = m_session->secretKeyHash(m_secretId);
    QString out;
    for (int i = 0; i < h.size(); ++i) {
        out += QString::fromLatin1("%1").arg(uchar(h.at(i)), 2, 16, QLatin1Char('0'));
        if (i % 2 == 1 && i + 1 < h.size()) out += QLatin1Char(' ');
    }
    return out.toUpper();
}

void MessagesModel::acceptSecret()
{
    if (m_secretId) m_session->acceptSecretChat(m_secretId);
}

void MessagesModel::discardSecret()
{
    if (m_secretId) m_session->discardSecretChat(m_secretId);
}

void MessagesModel::setSecretTtl(int seconds)
{
    if (m_secretId) m_session->setSecretTtl(m_secretId, seconds);
}

void MessagesModel::onSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out)
{
    Q_UNUSED(randomId);
    if (id != m_secretId) return;
    Row r;
    r.m.text = text;
    r.m.out = out;
    r.m.date = date;
    r.m.fromId = out ? m_session->selfId() : m_peer.id;
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
    emit messageAppended();
}

void MessagesModel::onSecretChatsChanged()
{
    if (m_secretId) emit peerChanged();   // state/ttl may have changed (accept/ready/ttl)
}
