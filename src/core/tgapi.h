// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The API layer above MTProto: builders for the requests this client sends, readers that
// turn the schema-parsed responses into the structs in tgtypes.h, and the cache of the
// users and chats that ride along with almost every response (messages name their sender
// by id alone, so without it every group message is anonymous).
#ifndef TGAPI_H
#define TGAPI_H

#include "tgtypes.h"
#include "tlobject.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QString>

class TgPeerCache
{
public:
    bool contains(const TgPeer &p) const { return m_infos.contains(p.key()); }
    TgPeerInfo info(const TgPeer &p) const { return m_infos.value(p.key()); }
    void put(const TgPeerInfo &info) { m_infos.insert(info.peer.key(), info); }
    /// The peer with its access hash filled in from the cache.
    TgPeer withHash(const TgPeer &p) const;
    QString title(const TgPeer &p) const;
    QString userName(qint64 userId) const { return title(TgPeer(TgPeer::User, userId)); }
    /// Takes the users and chats vectors out of any response that carries them.
    void absorb(const TlObject &response);
    void absorbUser(const TlObject &user);
    void absorbChat(const TlObject &chat);
    QList<TgPeerInfo> all() const { return m_infos.values(); }
    void clear() { m_infos.clear(); }
    /// Applies a status object (userStatus*) to a cached user.
    void setUserStatus(qint64 userId, const TlObject &status);

private:
    static void setUserStatusInfo(TgPeerInfo &info, const TlObject &status);
    QHash<QString, TgPeerInfo> m_infos;
};

struct TgQrLoginStep
{
    enum Status { ShowToken, Migrate, Success, PasswordNeeded };
    TgQrLoginStep() : status(ShowToken), expires(0), dcId(0) {}
    Status status;
    QByteArray token;
    int expires;
    int dcId;
    /// The URL to render as a QR code.
    QString url() const;
};

struct TgHistoryPage
{
    QList<TgMessage> messages;     // newest first, as the server sends them
};

struct TgDialogPage
{
    TgDialogPage() : hasMore(false) {}
    QList<TgDialog> dialogs;
    bool hasMore;
};

class TgApi
{
    Q_DECLARE_TR_FUNCTIONS(TgApi)
public:
    // -- builders --
    static QByteArray inputPeer(const TgPeer &p);
    static QByteArray inputPeerSelf();
    static QByteArray inputPeerEmpty();
    static QByteArray inputChannel(const TgPeer &p);
    static QByteArray helpGetNearestDc();
    static QByteArray exportLoginToken(int apiId, const QString &apiHash);
    static QByteArray importLoginToken(const QByteArray &token);
    // Phone-number login: request a code by SMS/app, then sign in with it.
    static QByteArray authSendCode(const QString &phone, int apiId, const QString &apiHash);
    static QByteArray authSignIn(const QString &phone, const QString &phoneCodeHash, const QString &code);
    static QByteArray authResendCode(const QString &phone, const QString &phoneCodeHash);
    static QByteArray accountGetPassword();
    static QByteArray authCheckPassword(qint64 srpId, const QByteArray &a, const QByteArray &m1);
    static QByteArray authLogOut();
    static QByteArray usersGetSelf();
    static QByteArray usersGetUser(const TgPeer &user);
    static QByteArray getDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, int limit, int folderId = 0);
    static QByteArray getDialogFilters();
    static QByteArray getHistory(const TgPeer &peer, int offsetId, int limit);
    static QByteArray sendMessage(const TgPeer &peer, const QString &text, qint64 randomId, int replyToId = 0);
    static QByteArray editMessage(const TgPeer &peer, int msgId, const QString &text);
    static QByteArray readHistory(const TgPeer &peer, int maxId);
    static QByteArray setTyping(const TgPeer &peer, bool typing);
    static QByteArray updateStatus(bool online);
    static QByteArray updatesGetState();
    static QByteArray updatesGetDifference(const TgUpdateState &state);
    static QByteArray resolveUsername(const QString &username);
    static QByteArray resolvePhone(const QString &phone);
    static QByteArray contactsSearch(const QString &query, int limit);
    static QByteArray contactsGetContacts();
    static QByteArray deleteHistory(const TgPeer &peer, bool justClear, bool revoke = false);
    static QByteArray deleteChatUser(qint64 chatId, bool revoke);          // leave a basic group
    static QByteArray leaveChannel(const TgPeer &channel);
    static QByteArray editPeerFolders(const TgPeer &peer, int folderId);   // 1 = archive, 0 = main
    static QByteArray updateDialogFilter(const QByteArray &filter, int id);
    static QByteArray deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke);
    static QByteArray updateNotifySettings(const TgPeer &peer, bool muted);
    /// upload.getFile for a location built by fileLocation()/peerPhotoLocation().
    static QByteArray getFile(const QByteArray &location, qint64 offset, int limit);
    static QByteArray fileLocation(const TgMedia &media, const QString &sizeType);
    static QByteArray peerPhotoLocation(const TgPeer &peer, qint64 photoId);
    static QByteArray saveFilePart(qint64 fileId, int part, int totalParts, bool big, const QByteArray &bytes);
    /// messages.sendMedia with an uploaded file as a photo or as a document.
    static QByteArray sendUploadedVoice(const TgPeer &peer, qint64 fileId, int parts, bool big, const QString &fileName,
                                        int durationSec, const QByteArray &waveform, qint64 randomId);
    static QByteArray sendUploadedMedia(const TgPeer &peer, qint64 fileId, int parts, bool big, const QString &fileName,
                                        bool asPhoto, const QString &mimeType, const QString &caption, qint64 randomId);
    static QByteArray inputUser(const TgPeer &user);
    // -- secret chats --
    static QByteArray getDhConfig(int version, int randomLength);
    static QByteArray requestEncryption(const TgPeer &user, int randomId, const QByteArray &gA);
    static QByteArray acceptEncryption(int chatId, qint64 accessHash, const QByteArray &gB, qint64 fingerprint);
    static QByteArray sendEncrypted(int chatId, qint64 accessHash, qint64 randomId, const QByteArray &data);
    static QByteArray sendEncryptedService(int chatId, qint64 accessHash, qint64 randomId, const QByteArray &data);
    static QByteArray discardEncryption(int chatId);
    static QByteArray readEncryptedHistory(int chatId, qint64 accessHash, int maxDate);
    static QByteArray exportAuthorization(int dcId);
    static QByteArray importAuthorization(qint64 id, const QByteArray &bytes);

    // -- readers --
    static TgQrLoginStep readLoginToken(const TlObject &o);
    static TgUpdateState readState(const TlObject &o);
    static TgMessage readMessage(const TlObject &m);
    /// updateShortMessage / updateShortChatMessage, which inline the message.
    static TgMessage readShortMessage(const TlObject &u, qint64 selfId);
    static TgDialogPage readDialogs(const TlObject &response, TgPeerCache &cache, bool archived = false);
    /// The custom folders from a messages.dialogFilters response.
    static QList<TgFolder> readFolders(const TlObject &response, qint64 selfId);
    /// The peer key of an InputPeer (user:/chat:/channel:), or empty for one we cannot key.
    static QString inputPeerKey(const TlObject &inputPeer, qint64 selfId);
    /// A folderPeer's peer key + its folder id (from updateFolderPeers).
    static QString folderPeerKey(const TlObject &folderPeer, int &folderId);
    static TgHistoryPage readHistory(const TlObject &response, TgPeerCache &cache);
    static TgPeer readPeer(const TlObject &peer);
    static QString describeMedia(const TlObject &media);
    /// The attachment of a MessageMedia (photo or document), or an invalid TgMedia.
    static TgMedia readMedia(const TlObject &media);
    /// The messages carried by an Updates object (the result of a send with media).
    static QList<TgMessage> messagesIn(const TlObject &updates);
    /// The mime type a file name suggests.
    static QString mimeTypeFor(const QString &fileName);
    static QString describeAction(const TlObject &action, const TgPeerCache &cache);
    /// The id the server gave a message we just sent, or 0.
    static int sentMessageId(const TlObject &updates);
    /// "@name", "name", "+1 999 000 1234" or a t.me link: what kind of lookup it needs.
    static bool looksLikePhone(const QString &value);
    static QString normalisePhone(const QString &value);
    static QString normaliseUsername(const QString &value);
    /// Base64 with the URL-safe alphabet and no padding (tg://login).
    static QString base64Url(const QByteArray &data);
};

#endif // TGAPI_H
