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

bool TgFolder::contains(const TgDialog &d, const TgPeerInfo &info, int now) const
{
    QString key = d.peer.key();
    // Named chats win over categories in both directions.
    if (exclude.contains(key)) return false;
    if (pinned.contains(key)) return true;
    if (include.contains(key)) return true;
    if (listedOnly) return false;                 // a shared folder is only what it names
    if (excludeArchived && d.archived) return false;
    if (excludeMuted && d.isMuted(now)) return false;
    if (excludeRead && d.unreadCount == 0) return false;
    // Category membership. Supergroups and channels are both "channel" on the wire and
    // cannot be told apart without the megagroup flag, so a folder asking for either takes
    // both rather than dropping chats the user sees elsewhere.
    if (d.peer.kind == TgPeer::Chat) return groups;
    if (d.peer.kind == TgPeer::Channel) return broadcasts || groups;
    if (info.isBot) return bots;
    if (info.isContact) return contacts;
    return nonContacts;
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
    if (u.has("photo")) {
        TlObject photo = u.obj("photo");
        info.photoId = photo.ctor() == Tl::UserProfilePhoto ? photo.longOr("photo_id") : 0;
        info.photoDcId = photo.intOr("dc_id");
    } else if (!min) {
        info.photoId = 0;
    }
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
    if (c.has("photo")) {
        TlObject photo = c.obj("photo");
        info.photoId = photo.ctor() == Tl::ChatPhoto ? photo.longOr("photo_id") : 0;
        info.photoDcId = photo.intOr("dc_id");
    }
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

QByteArray TgApi::authSendCode(const QString &phone, int apiId, const QString &apiHash)
{
    // codeSettings with flags=0: a plain SMS/app code, no flash-call or firebase tricks.
    TlWriter w(96);
    w.writeConstructor(Tl::AuthSendCode).writeString(phone).writeInt(apiId).writeString(apiHash)
     .writeConstructor(Tl::CodeSettings).writeInt(0);
    return w.toByteArray();
}

QByteArray TgApi::authSignIn(const QString &phone, const QString &phoneCodeHash, const QString &code)
{
    // flags bit 0 = phone_code is present (we always send the typed code).
    TlWriter w(96);
    w.writeConstructor(Tl::AuthSignIn).writeInt(1).writeString(phone).writeString(phoneCodeHash).writeString(code);
    return w.toByteArray();
}

QByteArray TgApi::authResendCode(const QString &phone, const QString &phoneCodeHash)
{
    // flags=0: no reason string.
    TlWriter w(64);
    w.writeConstructor(Tl::AuthResendCode).writeInt(0).writeString(phone).writeString(phoneCodeHash);
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

QByteArray TgApi::getDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, int limit, int folderId)
{
    // folder_id is always sent (bit 1): 0 is the main list (excludes the Archive), 1 is the
    // Archive. Leaving it out asks for every folder at once and mixes the archived chats in.
    TlWriter w(64);
    w.writeConstructor(Tl::MessagesGetDialogs).writeInt(1 << 1).writeInt(folderId)
     .writeInt(offsetDate).writeInt(offsetId)
     .writeRaw(offsetPeer.isNull() ? inputPeerEmpty() : inputPeer(offsetPeer))
     .writeInt(limit).writeLong(0);
    return w.toByteArray();
}

QByteArray TgApi::getDialogFilters()
{
    TlWriter w(4);
    w.writeConstructor(Tl::MessagesGetDialogFilters);
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

QByteArray TgApi::deleteHistory(const TgPeer &peer, bool justClear, bool revoke)
{
    TlWriter w(32);
    int flags = (justClear ? 1 : 0) | (revoke ? 2 : 0);
    w.writeConstructor(Tl::MessagesDeleteHistory).writeInt(flags).writeRaw(inputPeer(peer)).writeInt(0);
    return w.toByteArray();
}

QByteArray TgApi::deleteChatUser(qint64 chatId, bool revoke)
{
    TlWriter w(24);
    w.writeConstructor(Tl::MessagesDeleteChatUser).writeInt(revoke ? 1 : 0).writeLong(chatId)
     .writeConstructor(Tl::InputUserSelf);
    return w.toByteArray();
}

QByteArray TgApi::leaveChannel(const TgPeer &channel)
{
    TlWriter w(24);
    w.writeConstructor(Tl::ChannelsLeaveChannel).writeRaw(inputChannel(channel));
    return w.toByteArray();
}

QByteArray TgApi::editPeerFolders(const TgPeer &peer, int folderId)
{
    TlWriter fp(32);
    fp.writeConstructor(Tl::InputFolderPeer).writeRaw(inputPeer(peer)).writeInt(folderId);
    TlWriter w(48);
    w.writeConstructor(Tl::FoldersEditPeerFolders).writeConstructor(Tl::Vector).writeInt(1).writeRaw(fp.toByteArray());
    return w.toByteArray();
}

QByteArray TgApi::updateDialogFilter(const QByteArray &filter, int id)
{
    TlWriter w(filter.size() + 16);
    // flags.0 set: the filter is present (an absent filter would delete it).
    w.writeConstructor(Tl::MessagesUpdateDialogFilter).writeInt(1).writeInt(id).writeRaw(filter);
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

QByteArray TgApi::getFile(const QByteArray &location, qint64 offset, int limit)
{
    TlWriter w(location.size() + 24);
    w.writeConstructor(Tl::UploadGetFile).writeInt(0).writeRaw(location).writeLong(offset).writeInt(limit);
    return w.toByteArray();
}

QByteArray TgApi::fileLocation(const TgMedia &media, const QString &sizeType)
{
    TlWriter w(64);
    w.writeConstructor(media.kind == TgMedia::Photo ? Tl::InputPhotoFileLocation : Tl::InputDocumentFileLocation)
     .writeLong(media.id).writeLong(media.accessHash).writeBytes(media.fileReference).writeString(sizeType);
    return w.toByteArray();
}

QByteArray TgApi::peerPhotoLocation(const TgPeer &peer, qint64 photoId)
{
    TlWriter w(48);
    w.writeConstructor(Tl::InputPeerPhotoFileLocation).writeInt(0).writeRaw(inputPeer(peer)).writeLong(photoId);
    return w.toByteArray();
}

QByteArray TgApi::saveFilePart(qint64 fileId, int part, int totalParts, bool big, const QByteArray &bytes)
{
    TlWriter w(bytes.size() + 32);
    if (big) w.writeConstructor(Tl::UploadSaveBigFilePart).writeLong(fileId).writeInt(part).writeInt(totalParts).writeBytes(bytes);
    else w.writeConstructor(Tl::UploadSaveFilePart).writeLong(fileId).writeInt(part).writeBytes(bytes);
    return w.toByteArray();
}

QByteArray TgApi::sendUploadedVoice(const TgPeer &peer, qint64 fileId, int parts, bool big, const QString &fileName,
                                    int durationSec, const QByteArray &waveform, qint64 randomId)
{
    TlWriter file(64);
    if (big) file.writeConstructor(Tl::InputFileBig).writeLong(fileId).writeInt(parts).writeString(fileName);
    else file.writeConstructor(Tl::InputFile).writeLong(fileId).writeInt(parts).writeString(fileName).writeString(QString());

    // documentAttributeAudio with the voice flag (10) and the waveform (flag 2).
    TlWriter attr(waveform.size() + 32);
    attr.writeConstructor(Tl::DocumentAttributeAudio).writeInt((1 << 10) | (1 << 2)).writeInt(durationSec).writeBytes(waveform);

    TlWriter media(attr.length() + 64);
    media.writeConstructor(Tl::InputMediaUploadedDocument).writeInt(0)
         .writeRaw(file.toByteArray()).writeString(QLatin1String("audio/ogg"))
         .writeConstructor(Tl::Vector).writeInt(1).writeRaw(attr.toByteArray());

    TlWriter w(media.length() + 96);
    w.writeConstructor(Tl::MessagesSendMedia).writeInt(0).writeRaw(inputPeer(peer)).writeRaw(media.toByteArray())
     .writeString(QString()).writeLong(randomId);
    return w.toByteArray();
}

QByteArray TgApi::sendUploadedMedia(const TgPeer &peer, qint64 fileId, int parts, bool big, const QString &fileName,
                                    bool asPhoto, const QString &mimeType, const QString &caption, qint64 randomId)
{
    TlWriter file(64);
    if (big) file.writeConstructor(Tl::InputFileBig).writeLong(fileId).writeInt(parts).writeString(fileName);
    else file.writeConstructor(Tl::InputFile).writeLong(fileId).writeInt(parts).writeString(fileName).writeString(QString());

    TlWriter media(160);
    if (asPhoto) {
        media.writeConstructor(Tl::InputMediaUploadedPhoto).writeInt(0).writeRaw(file.toByteArray());
    } else {
        media.writeConstructor(Tl::InputMediaUploadedDocument).writeInt(1 << 4)      // force_file
             .writeRaw(file.toByteArray()).writeString(mimeType)
             .writeConstructor(Tl::Vector).writeInt(1)
             .writeConstructor(Tl::DocumentAttributeFilename).writeString(fileName);
    }
    TlWriter w(media.length() + 96);
    w.writeConstructor(Tl::MessagesSendMedia).writeInt(0).writeRaw(inputPeer(peer)).writeRaw(media.toByteArray())
     .writeString(caption).writeLong(randomId);
    return w.toByteArray();
}

QByteArray TgApi::inputUser(const TgPeer &user)
{
    TlWriter w(20);
    w.writeConstructor(Tl::InputUser).writeLong(user.id).writeLong(user.accessHash);
    return w.toByteArray();
}

QByteArray TgApi::getDhConfig(int version, int randomLength)
{
    TlWriter w(12);
    w.writeConstructor(Tl::MessagesGetDhConfig).writeInt(version).writeInt(randomLength);
    return w.toByteArray();
}

QByteArray TgApi::requestEncryption(const TgPeer &user, int randomId, const QByteArray &gA)
{
    TlWriter w(gA.size() + 32);
    w.writeConstructor(Tl::MessagesRequestEncryption).writeRaw(inputUser(user)).writeInt(randomId).writeBytes(gA);
    return w.toByteArray();
}

namespace
{
    QByteArray inputEncryptedChat(int chatId, qint64 accessHash)
    {
        TlWriter w(16);
        w.writeConstructor(Tl::InputEncryptedChat).writeInt(chatId).writeLong(accessHash);
        return w.toByteArray();
    }
}

QByteArray TgApi::acceptEncryption(int chatId, qint64 accessHash, const QByteArray &gB, qint64 fingerprint)
{
    TlWriter w(gB.size() + 32);
    w.writeConstructor(Tl::MessagesAcceptEncryption).writeRaw(inputEncryptedChat(chatId, accessHash)).writeBytes(gB).writeLong(fingerprint);
    return w.toByteArray();
}

QByteArray TgApi::sendEncrypted(int chatId, qint64 accessHash, qint64 randomId, const QByteArray &data)
{
    TlWriter w(data.size() + 32);
    w.writeConstructor(Tl::MessagesSendEncrypted).writeInt(0).writeRaw(inputEncryptedChat(chatId, accessHash)).writeLong(randomId).writeBytes(data);
    return w.toByteArray();
}

QByteArray TgApi::sendEncryptedService(int chatId, qint64 accessHash, qint64 randomId, const QByteArray &data)
{
    TlWriter w(data.size() + 32);
    w.writeConstructor(Tl::MessagesSendEncryptedService).writeRaw(inputEncryptedChat(chatId, accessHash)).writeLong(randomId).writeBytes(data);
    return w.toByteArray();
}

QByteArray TgApi::discardEncryption(int chatId)
{
    TlWriter w(12);
    w.writeConstructor(Tl::MessagesDiscardEncryption).writeInt(0).writeInt(chatId);
    return w.toByteArray();
}

QByteArray TgApi::readEncryptedHistory(int chatId, qint64 accessHash, int maxDate)
{
    TlWriter w(24);
    w.writeConstructor(Tl::MessagesReadEncryptedHistory).writeRaw(inputEncryptedChat(chatId, accessHash)).writeInt(maxDate);
    return w.toByteArray();
}

QByteArray TgApi::exportAuthorization(int dcId)
{
    TlWriter w(8);
    w.writeConstructor(Tl::AuthExportAuthorization).writeInt(dcId);
    return w.toByteArray();
}

QByteArray TgApi::importAuthorization(qint64 id, const QByteArray &bytes)
{
    TlWriter w(bytes.size() + 16);
    w.writeConstructor(Tl::AuthImportAuthorization).writeLong(id).writeBytes(bytes);
    return w.toByteArray();
}

QString TgApi::mimeTypeFor(const QString &fileName)
{
    QString ext = fileName.section(QLatin1Char('.'), -1).toLower();
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) return QLatin1String("image/jpeg");
    if (ext == QLatin1String("png")) return QLatin1String("image/png");
    if (ext == QLatin1String("gif")) return QLatin1String("image/gif");
    if (ext == QLatin1String("bmp")) return QLatin1String("image/bmp");
    if (ext == QLatin1String("mp4")) return QLatin1String("video/mp4");
    if (ext == QLatin1String("3gp")) return QLatin1String("video/3gpp");
    if (ext == QLatin1String("mp3")) return QLatin1String("audio/mpeg");
    if (ext == QLatin1String("aac")) return QLatin1String("audio/aac");
    if (ext == QLatin1String("amr")) return QLatin1String("audio/amr");
    if (ext == QLatin1String("txt")) return QLatin1String("text/plain");
    if (ext == QLatin1String("pdf")) return QLatin1String("application/pdf");
    if (ext == QLatin1String("zip")) return QLatin1String("application/zip");
    if (ext == QLatin1String("sis") || ext == QLatin1String("sisx")) return QLatin1String("x-epoc/x-sisx-app");
    return QLatin1String("application/octet-stream");
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

namespace
{
    /// Picks the photo size to show and the one to save: the largest that fits a bubble
    /// (up to 400 pixels) for display, the largest of all for saving.
    void chooseSizes(const QVariantList &sizes, TgMedia &m)
    {
        int bestShow = -1, bestBig = -1;
        for (int i = 0; i < sizes.size(); ++i) {
            TlObject sz = TlSchema::toObject(sizes.at(i));
            if (sz.ctor() == Tl::PhotoStrippedSize) { m.strippedThumb = sz.bytes("bytes"); continue; }
            if (sz.ctor() != Tl::PhotoSize && sz.ctor() != Tl::PhotoSizeProgressive && sz.ctor() != Tl::PhotoCachedSize) continue;
            int largest = qMax(sz.intOr("w"), sz.intOr("h"));
            if (largest <= 0) continue;
            if (largest <= 400 && largest > bestShow) {
                bestShow = largest;
                m.sizeType = sz.str("type");
                m.width = sz.intOr("w");
                m.height = sz.intOr("h");
            }
            if (largest > bestBig) {
                bestBig = largest;
                m.bigSizeType = sz.str("type");
                if (sz.ctor() == Tl::PhotoSize) m.fileSize = sz.intOr("size");
                else if (sz.ctor() == Tl::PhotoSizeProgressive) {
                    QVariantList l = sz.vec("sizes");
                    if (!l.isEmpty()) m.fileSize = l.last().toInt();
                }
            }
        }
        if (m.sizeType.isEmpty()) m.sizeType = m.bigSizeType;
    }

    QString smallestThumb(const QVariantList &thumbs)
    {
        QString best;
        int bestArea = 0x7fffffff;
        for (int i = 0; i < thumbs.size(); ++i) {
            TlObject sz = TlSchema::toObject(thumbs.at(i));
            if (sz.ctor() != Tl::PhotoSize && sz.ctor() != Tl::PhotoSizeProgressive) continue;
            int area = sz.intOr("w") * sz.intOr("h");
            if (area <= 0 || area >= bestArea) continue;
            bestArea = area;
            best = sz.str("type");
        }
        return best;
    }
}

TgMedia TgApi::readMedia(const TlObject &media)
{
    TgMedia m;
    if (media.ctor() == Tl::MessageMediaPhoto && media.has("photo")) {
        TlObject photo = media.obj("photo");
        if (photo.ctor() != Tl::Photo) return m;
        m.kind = TgMedia::Photo;
        m.id = photo.longOr("id");
        m.accessHash = photo.longOr("access_hash");
        m.fileReference = photo.bytes("file_reference");
        m.dcId = photo.intOr("dc_id");
        chooseSizes(photo.vec("sizes"), m);
        return m;
    }
    if (media.ctor() == Tl::MessageMediaDocument && media.has("document")) {
        TlObject doc = media.obj("document");
        if (doc.ctor() != Tl::Document) return m;
        m.kind = TgMedia::Document;
        m.id = doc.longOr("id");
        m.accessHash = doc.longOr("access_hash");
        m.fileReference = doc.bytes("file_reference");
        m.dcId = doc.intOr("dc_id");
        m.mimeType = doc.str("mime_type");
        m.fileSize = doc.longOr("size");
        if (doc.has("thumbs")) m.thumbSizeType = smallestThumb(doc.vec("thumbs"));
        QString audioTitle, audioPerformer;
        QVariantList attrs = doc.vec("attributes");
        for (int i = 0; i < attrs.size(); ++i) {
            TlObject a = TlSchema::toObject(attrs.at(i));
            if (a.ctor() == Tl::DocumentAttributeFilename) m.fileName = a.str("file_name");
            else if (a.ctor() == Tl::DocumentAttributeSticker) m.kind = TgMedia::Sticker;
            else if (a.ctor() == Tl::DocumentAttributeAnimated) m.kind = TgMedia::Gif;
            else if (a.ctor() == Tl::DocumentAttributeImageSize) { m.width = a.intOr("w"); m.height = a.intOr("h"); }
            else if (a.ctor() == Tl::DocumentAttributeVideo) {
                if (m.kind == TgMedia::Document) m.kind = TgMedia::Video;
                m.duration = int(a.doubleOr("duration"));
                m.width = a.intOr("w");
                m.height = a.intOr("h");
            }
            else if (a.ctor() == Tl::DocumentAttributeAudio) {
                m.kind = a.flag("flags", 10) ? TgMedia::Voice : TgMedia::Audio;
                m.duration = a.intOr("duration");
                audioTitle = a.str("title");
                audioPerformer = a.str("performer");
            }
        }
        if (m.mimeType == QLatin1String("image/webp") || m.mimeType == QLatin1String("application/x-tgsticker")) m.kind = TgMedia::Sticker;
        // Name music (audio) from its tags the way Telegram's clients do: "<title>_<performer>.<ext>"
        // (performer = the "Contributing Artists" tag). Tags win over any file-name attribute for
        // audio; other files keep their file name, falling back to a generic name when there is none.
        if (m.kind == TgMedia::Audio && !(audioTitle.isEmpty() && audioPerformer.isEmpty())) {
            QString ext = m.fileName.section(QLatin1Char('.'), -1).toLower();          // prefer the real extension
            if (ext.isEmpty() || ext.size() > 5) {
                ext = m.mimeType.section(QLatin1Char('/'), -1).toLower();
                if (ext == QLatin1String("mpeg") || ext == QLatin1String("mpeg3") || m.mimeType == QLatin1String("audio/mpeg")) ext = QLatin1String("mp3");
                else if (ext == QLatin1String("mp4") || ext == QLatin1String("x-m4a") || m.mimeType == QLatin1String("audio/mp4") || m.mimeType == QLatin1String("audio/x-m4a")) ext = QLatin1String("m4a");
            }
            if (ext.isEmpty()) ext = QLatin1String("mp3");
            QString base = audioTitle.isEmpty() ? audioPerformer
                         : (audioPerformer.isEmpty() ? audioTitle : audioTitle + QLatin1Char('_') + audioPerformer);
            m.fileName = base + QLatin1Char('.') + ext;
        } else if (m.fileName.isEmpty()) {
            QString ext = m.mimeType.section(QLatin1Char('/'), -1).toLower();
            m.fileName = QString::fromLatin1("file_%1.%2").arg(quint64(m.id), 0, 16).arg(ext.isEmpty() ? QLatin1String("bin") : ext);
        }
        return m;
    }
    return m;
}

QList<TgMessage> TgApi::messagesIn(const TlObject &updates)
{
    QList<TgMessage> out;
    if (updates.isNull()) return out;
    QVariantList list = updates.vec("updates");
    if (updates.has("update")) list.append(QVariant::fromValue(updates.obj("update")));
    for (int i = 0; i < list.size(); ++i) {
        TlObject u = TlSchema::toObject(list.at(i));
        if ((u.ctor() == Tl::UpdateNewMessage || u.ctor() == Tl::UpdateNewChannelMessage) && u.has("message"))
            out.append(readMessage(u.obj("message")));
    }
    return out;
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
    if (m.has("media")) {
        t.note = describeMedia(m.obj("media"));
        t.media = readMedia(m.obj("media"));
    }
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

TgDialogPage TgApi::readDialogs(const TlObject &response, TgPeerCache &cache, bool archived)
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
        entry.archived = archived;
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

QString TgApi::inputPeerKey(const TlObject &p, qint64 selfId)
{
    switch (p.ctor()) {
    case Tl::InputPeerSelf: return TgPeer(TgPeer::User, selfId).key();
    case Tl::InputPeerUser: case Tl::InputPeerUserFromMessage: return TgPeer(TgPeer::User, p.longOr("user_id")).key();
    case Tl::InputPeerChat: return TgPeer(TgPeer::Chat, p.longOr("chat_id")).key();
    case Tl::InputPeerChannel: case Tl::InputPeerChannelFromMessage: return TgPeer(TgPeer::Channel, p.longOr("channel_id")).key();
    default: return QString();
    }
}

QString TgApi::folderPeerKey(const TlObject &fp, int &folderId)
{
    folderId = fp.intOr("folder_id");
    return fp.has("peer") ? readPeer(fp.obj("peer")).key() : QString();
}

namespace
{
    void collectKeys(const TlObject &filter, const char *field, QList<QString> &out, qint64 selfId)
    {
        QVariantList v = filter.vec(field);
        for (int i = 0; i < v.size(); ++i) {
            QString k = TgApi::inputPeerKey(TlSchema::toObject(v.at(i)), selfId);
            if (!k.isEmpty()) out.append(k);
        }
    }
}

QList<TgFolder> TgApi::readFolders(const TlObject &response, qint64 selfId)
{
    QList<TgFolder> folders;
    QVariantList filters = response.vec("filters");
    for (int i = 0; i < filters.size(); ++i) {
        TlObject f = TlSchema::toObject(filters.at(i));
        // dialogFilterDefault is the main list itself, not a folder to list alongside.
        if (f.ctor() != Tl::DialogFilter && f.ctor() != Tl::DialogFilterChatlist) continue;
        TgFolder folder;
        folder.id = f.intOr("id");
        folder.listedOnly = f.ctor() == Tl::DialogFilterChatlist;
        if (f.has("title")) {
            TlObject t = f.obj("title");
            folder.title = t.str("text");
        }
        if (folder.title.isEmpty()) folder.title = tr("Folder");
        folder.contacts = f.flag("flags", 0);
        folder.nonContacts = f.flag("flags", 1);
        folder.groups = f.flag("flags", 2);
        folder.broadcasts = f.flag("flags", 3);
        folder.bots = f.flag("flags", 4);
        folder.excludeMuted = f.flag("flags", 11);
        folder.excludeRead = f.flag("flags", 12);
        folder.excludeArchived = f.flag("flags", 13);
        folder.emoticon = f.str("emoticon");
        folder.hasColor = f.has("color");
        folder.color = f.intOr("color", -1);
        collectKeys(f, "pinned_peers", folder.pinned, selfId);
        collectKeys(f, "include_peers", folder.include, selfId);
        collectKeys(f, "exclude_peers", folder.exclude, selfId);
        folders.append(folder);
    }
    return folders;
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
