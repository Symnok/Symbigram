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
    static QByteArray accountGetPassword();
    static QByteArray authCheckPassword(qint64 srpId, const QByteArray &a, const QByteArray &m1);
    static QByteArray authLogOut();
    static QByteArray usersGetSelf();
    static QByteArray usersGetUser(const TgPeer &user);
    static QByteArray getDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, int limit);
    static QByteArray getHistory(const TgPeer &peer, int offsetId, int limit);
    static QByteArray sendMessage(const TgPeer &peer, const QString &text, qint64 randomId, int replyToId = 0);
    static QByteArray readHistory(const TgPeer &peer, int maxId);
    static QByteArray setTyping(const TgPeer &peer, bool typing);
    static QByteArray updateStatus(bool online);
    static QByteArray updatesGetState();
    static QByteArray updatesGetDifference(const TgUpdateState &state);
    static QByteArray resolveUsername(const QString &username);
    static QByteArray resolvePhone(const QString &phone);
    static QByteArray contactsSearch(const QString &query, int limit);
    static QByteArray contactsGetContacts();
    static QByteArray deleteHistory(const TgPeer &peer, bool justClear);
    static QByteArray deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke);
    static QByteArray updateNotifySettings(const TgPeer &peer, bool muted);

    // -- readers --
    static TgQrLoginStep readLoginToken(const TlObject &o);
    static TgUpdateState readState(const TlObject &o);
    static TgMessage readMessage(const TlObject &m);
    /// updateShortMessage / updateShortChatMessage, which inline the message.
    static TgMessage readShortMessage(const TlObject &u, qint64 selfId);
    static TgDialogPage readDialogs(const TlObject &response, TgPeerCache &cache);
    static TgHistoryPage readHistory(const TlObject &response, TgPeerCache &cache);
    static TgPeer readPeer(const TlObject &peer);
    static QString describeMedia(const TlObject &media);
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
