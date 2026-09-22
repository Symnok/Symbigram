// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "tgapi.h"
#include "tlconstructors.h"
#include "tlwriter.h"

#include <QStringList>

// -- TgPeer -------------------------------------------------------------------------------------------------

QString TgPeer::key() const
{
    const char *k = kind == User ? "user" : (kind == Chat ? "chat" : "channel");
    return QLatin1String(k) + QLatin1Char(':') + QString::number(id);
}

TgPeer TgPeer::fromKey(const QString &key)
{
    TgPeer p;
    int colon = key.indexOf(QLatin1Char(':'));
    if (colon < 0) return p;
    QString k = key.left(colon);
    p.kind = k == QLatin1String("chat") ? Chat : (k == QLatin1String("channel") ? Channel : User);
    p.id = key.mid(colon + 1).toLongLong();
    return p;
}

// -- TgPeerCache ---------------------------------------------------------------------------------------------

TgPeer TgPeerCache::withHash(const TgPeer &p) const
{
    if (p.accessHash != 0 || p.kind == TgPeer::Chat) return p;
    TgPeer r = p;
    if (m_infos.contains(p.key())) r.accessHash = m_infos.value(p.key()).peer.accessHash;
    return r;
}

QString TgPeerCache::title(const TgPeer &p) const
{
    if (m_infos.contains(p.key())) return m_infos.value(p.key()).title;
    return (p.kind == TgPeer::User ? TgApi::tr("user %1") : TgApi::tr("chat %1")).arg(p.id);
}

void TgPeerCache::absorb(const TlObject &response)
{
    if (response.isNull()) return;
    QVariantList users = response.vec("users");
    for (int i = 0; i < users.size(); ++i) absorbUser(TlSchema::toObject(users.at(i)));
    QVariantList chats = response.vec("chats");
    for (int i = 0; i < chats.size(); ++i) absorbChat(TlSchema::toObject(chats.at(i)));
}

void TgPeerCache::absorbUser(const TlObject &u)
{
    if (u.isNull() || !u.has("id")) return;
    qint64 id = u.longOr("id");
    TgPeerInfo info = m_infos.value(TgPeer(TgPeer::User, id).key());
    info.peer = TgPeer(TgPeer::User, id, u.longOr("access_hash", info.peer.accessHash));
    if (u.ctor() == Tl::UserEmpty) {
        info.title = TgApi::tr("deleted account");
        info.isDeleted = true;
        m_infos.insert(info.peer.key(), info);
        return;
    }
    // A "min" user (flags.20) carries incomplete fields; keep what a full one gave us.
    bool min = u.flag("flags", 20);
    if (u.has("first_name") || !min) info.firstName = u.str("first_name");
    if (u.has("last_name") || !min) info.lastName = u.str("last_name");
    if (u.has("username")) info.username = u.str("username");
    if (u.has("phone")) info.phone = u.str("phone");
    info.isSelf = u.flag("flags", 10);
    info.isContact = u.flag("flags", 11);
    info.isDeleted = u.flag("flags", 13);
    info.isBot = u.flag("flags", 14);
    QString name = (info.firstName + QLatin1Char(' ') + info.lastName).trimmed();
    if (name.isEmpty()) name = info.username;
    if (name.isEmpty()) name = info.isDeleted ? TgApi::tr("deleted account") : TgApi::tr("user %1").arg(id);
    info.title = name;
    if (u.has("status")) setUserStatusInfo(info, u.obj("status"));
    m_infos.insert(info.peer.key(), info);
}

void TgPeerCache::absorbChat(const TlObject &c)
{
    if (c.isNull() || !c.has("id")) return;
    qint64 id = c.longOr("id");
    bool channel = c.ctor() == Tl::Channel || c.ctor() == Tl::ChannelForbidden;
    TgPeer peer(channel ? TgPeer::Channel : TgPeer::Chat, id, c.longOr("access_hash"));
    TgPeerInfo info = m_infos.value(peer.key());
    if (peer.accessHash == 0) peer.accessHash = info.peer.accessHash;
    info.peer = peer;
    QString title = c.str("title");
    info.title = title.isEmpty() ? TgApi::tr("chat %1").arg(id) : title;
    info.username = c.str("username");
    info.membersCount = c.intOr("participants_count", info.membersCount);
    info.isBroadcast = channel && c.flag("flags", 5) && !c.flag("flags", 8);
    m_infos.insert(peer.key(), info);
}

void TgPeerCache::setUserStatusInfo(TgPeerInfo &info, const TlObject &status)
{
    switch (status.ctor()) {
    case Tl::UserStatusOnline: info.online = true; info.statusKind = 1; info.lastSeen = status.intOr("expires"); break;
    case Tl::UserStatusOffline: info.online = false; info.statusKind = 2; info.lastSeen = status.intOr("was_online"); break;
    case Tl::UserStatusRecently: info.online = false; info.statusKind = 3; break;
    case Tl::UserStatusLastWeek: info.online = false; info.statusKind = 4; break;
    case Tl::UserStatusLastMonth: info.online = false; info.statusKind = 5; break;
    case Tl::UserStatusEmpty: info.online = false; info.statusKind = 6; break;
    default: break;
    }
}

void TgPeerCache::setUserStatus(qint64 userId, const TlObject &status)
{
    QString key = TgPeer(TgPeer::User, userId).key();
    if (!m_infos.contains(key)) return;
    TgPeerInfo info = m_infos.value(key);
    setUserStatusInfo(info, status);
    m_infos.insert(key, info);
}

// -- TgQrLoginStep -------------------------------------------------------------------------------------------

QString TgQrLoginStep::url() const
{
    return token.isEmpty() ? QString() : QLatin1String("tg://login?token=") + TgApi::base64Url(token);
}

QString TgApi::base64Url(const QByteArray &data)
{
    QString s = QString::fromLatin1(data.toBase64());
    s.replace(QLatin1Char('+'), QLatin1Char('-'));
    s.replace(QLatin1Char('/'), QLatin1Char('_'));
    while (s.endsWith(QLatin1Char('='))) s.chop(1);
    return s;
}

// -- builders ------------------------------------------------------------------------------------------------

QByteArray TgApi::inputPeer(const TgPeer &p)
{
    TlWriter w(20);
    if (p.kind == TgPeer::Chat) w.writeConstructor(Tl::InputPeerChat).writeLong(p.id);
    else if (p.kind == TgPeer::Channel) w.writeConstructor(Tl::InputPeerChannel).writeLong(p.id).writeLong(p.accessHash);
    else w.writeConstructor(Tl::InputPeerUser).writeLong(p.id).writeLong(p.accessHash);
    return w.toByteArray();
}

QByteArray TgApi::inputPeerSelf() { TlWriter w(4); w.writeConstructor(Tl::InputPeerSelf); return w.toByteArray(); }
QByteArray TgApi::inputPeerEmpty() { TlWriter w(4); w.writeConstructor(Tl::InputPeerEmpty); return w.toByteArray(); }

QByteArray TgApi::inputChannel(const TgPeer &p)
{
    TlWriter w(20);
    w.writeConstructor(Tl::InputChannel).writeLong(p.id).writeLong(p.accessHash);
    return w.toByteArray();
}

QByteArray TgApi::helpGetNearestDc() { TlWriter w(4); w.writeConstructor(Tl::HelpGetNearestDc); return w.toByteArray(); }

QByteArray TgApi::exportLoginToken(int apiId, const QString &apiHash)
{
    TlWriter w(64);
    w.writeConstructor(Tl::AuthExportLoginToken).writeInt(apiId).writeString(apiHash).writeConstructor(Tl::Vector).writeInt(0);
    return w.toByteArray();
}

QByteArray TgApi::importLoginToken(const QByteArray &token)
{
    TlWriter w(token.size() + 16);
    w.writeConstructor(Tl::AuthImportLoginToken).writeBytes(token);
    return w.toByteArray();
}

QByteArray TgApi::accountGetPassword() { TlWriter w(4); w.writeConstructor(Tl::AccountGetPassword); return w.toByteArray(); }

QByteArray TgApi::authCheckPassword(qint64 srpId, const QByteArray &a, const QByteArray &m1)
{
    TlWriter w(600);
    w.writeConstructor(Tl::AuthCheckPassword).writeConstructor(Tl::InputCheckPasswordSrp).writeLong(srpId).writeBytes(a).writeBytes(m1);
    return w.toByteArray();
}

QByteArray TgApi::authLogOut() { TlWriter w(4); w.writeConstructor(Tl::AuthLogOut); return w.toByteArray(); }

QByteArray TgApi::usersGetSelf()
{
    TlWriter w(16);
    w.writeConstructor(Tl::UsersGetUsers).writeConstructor(Tl::Vector).writeInt(1).writeConstructor(Tl::InputUserSelf);
    return w.toByteArray();
}

QByteArray TgApi::usersGetUser(const TgPeer &user)
{
    TlWriter w(32);
    w.writeConstructor(Tl::UsersGetUsers).writeConstructor(Tl::Vector).writeInt(1)
     .writeConstructor(Tl::InputUser).writeLong(user.id).writeLong(user.accessHash);
    return w.toByteArray();
}

QByteArray TgApi::getDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, int limit)
{
    // folder_id 0 is always sent: leaving it out asks for every folder at once and puts
    // the archived chats back among the ordinary ones.
    TlWriter w(64);
    w.writeConstructor(Tl::MessagesGetDialogs).writeInt(1 << 1).writeInt(0)
     .writeInt(offsetDate).writeInt(offsetId)
     .writeRaw(offsetPeer.isNull() ? inputPeerEmpty() : inputPeer(offsetPeer))
     .writeInt(limit).writeLong(0);
    return w.toByteArray();
}

QByteArray TgApi::getHistory(const TgPeer &peer, int offsetId, int limit)
{
    TlWriter w(64);
    w.writeConstructor(Tl::MessagesGetHistory).writeRaw(inputPeer(peer))
     .writeInt(offsetId).writeInt(0).writeInt(0).writeInt(limit).writeInt(0).writeInt(0).writeLong(0);
    return w.toByteArray();
}

QByteArray TgApi::sendMessage(const TgPeer &peer, const QString &text, qint64 randomId, int replyToId)
{
    TlWriter w(text.size() * 3 + 64);
    w.writeConstructor(Tl::MessagesSendMessage).writeInt(replyToId ? 1 : 0).writeRaw(inputPeer(peer));
    if (replyToId) w.writeConstructor(Tl::InputReplyToMessage).writeInt(0).writeInt(replyToId);
    w.writeString(text).writeLong(randomId);
    return w.toByteArray();
}

QByteArray TgApi::readHistory(const TgPeer &peer, int maxId)
{
    TlWriter w(32);
    if (peer.kind == TgPeer::Channel) w.writeConstructor(Tl::ChannelsReadHistory).writeRaw(inputChannel(peer)).writeInt(maxId);
    else w.writeConstructor(Tl::MessagesReadHistory).writeRaw(inputPeer(peer)).writeInt(maxId);
    return w.toByteArray();
}

QByteArray TgApi::setTyping(const TgPeer &peer, bool typing)
{
    TlWriter w(32);
    w.writeConstructor(Tl::MessagesSetTyping).writeInt(0).writeRaw(inputPeer(peer))
     .writeConstructor(typing ? Tl::SendMessageTypingAction : Tl::SendMessageCancelAction);
    return w.toByteArray();
}

QByteArray TgApi::updateStatus(bool online)
{
    TlWriter w(8);
    w.writeConstructor(Tl::AccountUpdateStatus).writeBool(!online);      // the field is "offline"
    return w.toByteArray();
}

QByteArray TgApi::updatesGetState() { TlWriter w(4); w.writeConstructor(Tl::UpdatesGetState); return w.toByteArray(); }

QByteArray TgApi::updatesGetDifference(const TgUpdateState &state)
{
    TlWriter w(24);
    w.writeConstructor(Tl::UpdatesGetDifference).writeInt(0).writeInt(state.pts).writeInt(state.date).writeInt(state.qts);
    return w.toByteArray();
}

QByteArray TgApi::resolveUsername(const QString &username)
{
    TlWriter w(64);
    w.writeConstructor(Tl::ContactsResolveUsername).writeInt(0).writeString(username);
    return w.toByteArray();
}

QByteArray TgApi::resolvePhone(const QString &phone)
{
    TlWriter w(32);
    w.writeConstructor(Tl::ContactsResolvePhone).writeString(phone);
    return w.toByteArray();
}

QByteArray TgApi::contactsSearch(const QString &query, int limit)
{
    TlWriter w(64);
    w.writeConstructor(Tl::ContactsSearch).writeInt(0).writeString(query).writeInt(limit);
    return w.toByteArray();
}

QByteArray TgApi::contactsGetContacts()
{
    TlWriter w(16);
    w.writeConstructor(Tl::ContactsGetContacts).writeLong(0);
    return w.toByteArray();
}

QByteArray TgApi::deleteHistory(const TgPeer &peer, bool justClear)
{
    TlWriter w(32);
    w.writeConstructor(Tl::MessagesDeleteHistory).writeInt(justClear ? 1 : 0).writeRaw(inputPeer(peer)).writeInt(0);
    return w.toByteArray();
}

QByteArray TgApi::deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke)
{
    TlWriter w(32 + ids.size() * 4);
    if (peer.kind == TgPeer::Channel) w.writeConstructor(Tl::ChannelsDeleteMessages).writeRaw(inputChannel(peer));
    else w.writeConstructor(Tl::MessagesDeleteMessages).writeInt(revoke ? 1 : 0);
    w.writeVectorOfInt(ids);
    return w.toByteArray();
}

QByteArray TgApi::updateNotifySettings(const TgPeer &peer, bool muted)
{
    TlWriter w(64);
    w.writeConstructor(Tl::AccountUpdateNotifySettings)
     .writeConstructor(Tl::InputNotifyPeer).writeRaw(inputPeer(peer))
     .writeConstructor(Tl::InputPeerNotifySettings).writeInt(1 << 2).writeInt(muted ? 0x7fffffff : 0);
    return w.toByteArray();
}

// -- readers -------------------------------------------------------------------------------------------------

TgQrLoginStep TgApi::readLoginToken(const TlObject &o)
{
    TgQrLoginStep step;
    if (o.ctor() == Tl::AuthLoginToken) {
        step.status = TgQrLoginStep::ShowToken;
        step.token = o.bytes("token");
        step.expires = o.intOr("expires");
    } else if (o.ctor() == Tl::AuthLoginTokenMigrateTo) {
        step.status = TgQrLoginStep::Migrate;
        step.dcId = o.intOr("dc_id");
        step.token = o.bytes("token");
    } else if (o.ctor() == Tl::AuthLoginTokenSuccess) {
        step.status = TgQrLoginStep::Success;
    } else {
        throw TlException(QString::fromLatin1("unexpected auth.LoginToken 0x%1").arg(o.ctor(), 8, 16, QLatin1Char('0')));
    }
    return step;
}

TgUpdateState TgApi::readState(const TlObject &o)
{
    TgUpdateState s;
    s.pts = o.intOr("pts");
    s.qts = o.intOr("qts");
    s.date = o.intOr("date");
    s.seq = o.intOr("seq");
    return s;
}

TgPeer TgApi::readPeer(const TlObject &peer)
{
    if (peer.ctor() == Tl::PeerUser) return TgPeer(TgPeer::User, peer.longOr("user_id"));
    if (peer.ctor() == Tl::PeerChat) return TgPeer(TgPeer::Chat, peer.longOr("chat_id"));
    if (peer.ctor() == Tl::PeerChannel) return TgPeer(TgPeer::Channel, peer.longOr("channel_id"));
    return TgPeer();
}

namespace
{
    QString clock(int seconds)
    {
        return QString::fromLatin1("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
}

QString TgApi::describeMedia(const TlObject &media)
{
    switch (media.ctor()) {
    case Tl::MessageMediaPhoto: return media.has("video") ? tr("video") : tr("photo");
    case Tl::MessageMediaGeo:
    case Tl::MessageMediaGeoLive: {
        TlObject g = media.obj("geo");
        if (g.ctor() == Tl::GeoPoint)
            return tr("location %1, %2").arg(g.doubleOr("lat"), 0, 'f', 5).arg(g.doubleOr("long"), 0, 'f', 5);
        return tr("location");
    }
    case Tl::MessageMediaVenue: return tr("location: %1").arg(media.str("title"));
    case Tl::MessageMediaContact: return tr("contact: %1 %2 %3").arg(media.str("first_name"), media.str("last_name"), media.str("phone_number")).simplified();
    case Tl::MessageMediaWebPage: return QString();          // a link preview: the text itself carries the link
    case Tl::MessageMediaPoll: {
        TlObject poll = media.obj("poll");
        TlObject q = poll.obj("question");
        return tr("poll: %1").arg(q.str("text"));
    }
    case Tl::MessageMediaDice: return tr("dice: %1").arg(media.intOr("value"));
    case Tl::MessageMediaGame: return tr("game");
    case Tl::MessageMediaInvoice: return tr("invoice");
    case Tl::MessageMediaStory: return tr("story");
    case Tl::MessageMediaDocument: {
        TlObject doc = media.obj("document");
        if (doc.isNull() || doc.ctor() != Tl::Document) return tr("file");
        QString fileName, sticker;
        int duration = 0;
        bool voice = false, video = false, round = false, animated = false, audio = false, image = false;
        QVariantList attrs = doc.vec("attributes");
        for (int i = 0; i < attrs.size(); ++i) {
            TlObject a = TlSchema::toObject(attrs.at(i));
            if (a.ctor() == Tl::DocumentAttributeFilename) fileName = a.str("file_name");
            else if (a.ctor() == Tl::DocumentAttributeSticker) sticker = a.str("alt");
            else if (a.ctor() == Tl::DocumentAttributeAnimated) animated = true;
            else if (a.ctor() == Tl::DocumentAttributeImageSize) image = true;
            else if (a.ctor() == Tl::DocumentAttributeVideo) { video = true; duration = int(a.doubleOr("duration")); round = a.flag("flags", 0); }
            else if (a.ctor() == Tl::DocumentAttributeAudio) { audio = true; duration = a.intOr("duration"); voice = a.flag("flags", 10); }
        }
        if (!sticker.isEmpty() || doc.str("mime_type") == QLatin1String("image/webp") || doc.str("mime_type") == QLatin1String("application/x-tgsticker"))
            return sticker.isEmpty() ? tr("sticker") : tr("sticker %1").arg(sticker);
        if (voice) return duration ? tr("voice message %1").arg(clock(duration)) : tr("voice message");
        if (round) return duration ? tr("video message %1").arg(clock(duration)) : tr("video message");
        if (animated) return tr("GIF");
        if (video) return duration ? tr("video %1").arg(clock(duration)) : tr("video");
        if (audio) return duration ? tr("audio %1").arg(clock(duration)) : tr("audio");
        if (image && fileName.isEmpty()) return tr("image");
        qint64 size = doc.longOr("size");
        QString sizeText = size >= 1048576 ? tr("%1 MB").arg(double(size) / 1048576.0, 0, 'f', 1)
                         : (size >= 1024 ? tr("%1 KB").arg(int(size / 1024)) : tr("%1 B").arg(size));
        return tr("file: %1 (%2)").arg(fileName.isEmpty() ? doc.str("mime_type") : fileName, sizeText);
    }
    case Tl::MessageMediaUnsupported: return tr("unsupported attachment");
    default: return tr("attachment");
    }
}

QString TgApi::describeAction(const TlObject &action, const TgPeerCache &cache)
{
    switch (action.ctor()) {
    case Tl::MessageActionChatCreate: return tr("created the group \"%1\"").arg(action.str("title"));
    case Tl::MessageActionChannelCreate: return tr("created the channel \"%1\"").arg(action.str("title"));
    case Tl::MessageActionChatAddUser: {
        QStringList names;
        QVariantList users = action.vec("users");
        for (int i = 0; i < users.size(); ++i) names.append(cache.userName(users.at(i).toLongLong()));
        return tr("added %1").arg(names.join(QLatin1String(", ")));
    }
    case Tl::MessageActionChatDeleteUser: return tr("removed %1").arg(cache.userName(action.longOr("user_id")));
    case Tl::MessageActionChatJoinedByLink: return tr("joined by invite link");
    case Tl::MessageActionPinMessage: return tr("pinned a message");
    case Tl::MessageActionContactSignUp: return tr("joined Telegram");
    case Tl::MessageActionHistoryClear: return tr("cleared the history");
    default: return tr("service message");
    }
}

TgMessage TgApi::readMessage(const TlObject &m)
{
    TgMessage t;
    t.id = m.intOr("id");
    if (m.ctor() == Tl::MessageEmpty) { t.service = true; t.note = tr("empty message"); return t; }
    t.date = m.intOr("date");
    t.out = m.flag("flags", 1);
    t.mentioned = m.flag("flags", 4);
    if (m.has("from_id")) t.fromId = readPeer(m.obj("from_id")).id;
    if (m.has("peer_id")) t.peer = readPeer(m.obj("peer_id"));
    if (m.ctor() == Tl::MessageService) {
        t.service = true;
        t.note = QString();                 // filled by the caller, which has the peer cache
        return t;
    }
    t.text = m.str("message");
    t.editDate = m.intOr("edit_date");
    t.viaBot = m.has("via_bot_id");
    if (m.has("media")) t.note = describeMedia(m.obj("media"));
    if (m.has("fwd_from")) {
        TlObject fwd = m.obj("fwd_from");
        t.forwardedFrom = fwd.str("from_name");
        if (t.forwardedFrom.isEmpty() && fwd.has("from_id")) t.forwardedFrom = readPeer(fwd.obj("from_id")).key();
        if (t.forwardedFrom.isEmpty()) t.forwardedFrom = QLatin1String("?");
    }
    if (m.has("reply_to")) t.replyToId = m.obj("reply_to").intOr("reply_to_msg_id");
    return t;
}

TgMessage TgApi::readShortMessage(const TlObject &u, qint64 selfId)
{
    TgMessage m;
    m.id = u.intOr("id");
    m.date = u.intOr("date");
    m.out = u.flag("flags", 1);
    m.mentioned = u.flag("flags", 4);
    m.text = u.str("message");
    m.viaBot = u.has("via_bot_id");
    if (u.ctor() == Tl::UpdateShortChatMessage) {
        m.fromId = u.longOr("from_id");
        m.peer = TgPeer(TgPeer::Chat, u.longOr("chat_id"));
    } else {
        // updateShortMessage: user_id is the other party; the sender is us when out is set.
        qint64 other = u.longOr("user_id");
        m.peer = TgPeer(TgPeer::User, other);
        m.fromId = m.out ? selfId : other;
    }
    if (u.has("fwd_from")) {
        TlObject fwd = u.obj("fwd_from");
        m.forwardedFrom = fwd.str("from_name");
        if (m.forwardedFrom.isEmpty() && fwd.has("from_id")) m.forwardedFrom = readPeer(fwd.obj("from_id")).key();
        if (m.forwardedFrom.isEmpty()) m.forwardedFrom = QLatin1String("?");
    }
    if (u.has("reply_to")) m.replyToId = u.obj("reply_to").intOr("reply_to_msg_id");
    return m;
}

TgDialogPage TgApi::readDialogs(const TlObject &response, TgPeerCache &cache)
{
    TgDialogPage page;
    cache.absorb(response);

    // The newest message of each chat rides along in "messages", keyed by peer.
    QHash<QString, TlObject> lastMessages;
    QVariantList messages = response.vec("messages");
    for (int i = 0; i < messages.size(); ++i) {
        TlObject m = TlSchema::toObject(messages.at(i));
        if (!m.has("peer_id")) continue;
        lastMessages.insert(readPeer(m.obj("peer_id")).key(), m);
    }

    QVariantList dialogs = response.vec("dialogs");
    for (int i = 0; i < dialogs.size(); ++i) {
        TlObject d = TlSchema::toObject(dialogs.at(i));
        if (d.ctor() != Tl::Dialog || !d.has("peer")) continue;
        TgDialog entry;
        entry.peer = cache.withHash(readPeer(d.obj("peer")));
        entry.topMessageId = d.intOr("top_message");
        entry.unreadCount = d.intOr("unread_count");
        entry.readInboxMaxId = d.intOr("read_inbox_max_id");
        entry.readOutboxMaxId = d.intOr("read_outbox_max_id");
        entry.pinned = d.flag("flags", 2);
        if (d.has("notify_settings")) entry.mutedUntil = d.obj("notify_settings").intOr("mute_until");
        TlObject last = lastMessages.value(entry.peer.key());
        if (!last.isNull()) {
            TgMessage m = readMessage(last);
            if (m.service && last.has("action")) m.note = describeAction(last.obj("action"), cache);
            entry.topMessageDate = m.date;
            entry.lastText = m.text.isEmpty() ? m.note : m.text;
            entry.lastOut = m.out;
            entry.lastFromId = m.fromId;
        }
        page.dialogs.append(entry);
    }
    page.hasMore = response.ctor() == Tl::MessagesDialogsSlice && !page.dialogs.isEmpty();
    return page;
}

TgHistoryPage TgApi::readHistory(const TlObject &response, TgPeerCache &cache)
{
    TgHistoryPage page;
    cache.absorb(response);
    QVariantList messages = response.vec("messages");
    for (int i = 0; i < messages.size(); ++i) {
        TlObject o = TlSchema::toObject(messages.at(i));
        TgMessage m = readMessage(o);
        if (m.service && o.has("action")) m.note = describeAction(o.obj("action"), cache);
        page.messages.append(m);
    }
    return page;
}

int TgApi::sentMessageId(const TlObject &updates)
{
    if (updates.isNull()) return 0;
    if (updates.ctor() == Tl::UpdateShortSentMessage || updates.ctor() == Tl::UpdateMessageID) return updates.intOr("id");
    if (updates.has("update")) {
        int id = sentMessageId(updates.obj("update"));
        if (id) return id;
    }
    QVariantList list = updates.vec("updates");
    for (int i = 0; i < list.size(); ++i) {
        int id = sentMessageId(TlSchema::toObject(list.at(i)));
        if (id) return id;
    }
    return 0;
}

// -- lookups -----------------------------------------------------------------------------------------------------

namespace
{
    QString stripSeparators(const QString &value)
    {
        QString out;
        for (int i = 0; i < value.size(); ++i) {
            QChar c = value.at(i);
            if (c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('(') || c == QLatin1Char(')') || c == QLatin1Char('.')) continue;
            out.append(c);
        }
        return out;
    }
}

bool TgApi::looksLikePhone(const QString &value)
{
    QString stripped = stripSeparators(value.trimmed());
    if (stripped.isEmpty()) return false;
    int start = stripped.at(0) == QLatin1Char('+') ? 1 : 0;
    if (start >= stripped.size()) return false;
    for (int i = start; i < stripped.size(); ++i)
        if (!stripped.at(i).isDigit()) return false;
    return stripped.size() - start >= 5;
}

QString TgApi::normalisePhone(const QString &value)
{
    QString out;
    for (int i = 0; i < value.size(); ++i)
        if (value.at(i).isDigit()) out.append(value.at(i));
    return out;
}

QString TgApi::normaliseUsername(const QString &value)
{
    QString name = value.trimmed();
    if (name.startsWith(QLatin1Char('@'))) name = name.mid(1);
    int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0 && slash + 1 < name.size()) name = name.mid(slash + 1);
    return name;
}
