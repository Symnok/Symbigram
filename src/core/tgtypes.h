// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// What the client keeps of Telegram's objects: enough for a text messenger, nothing more.
#ifndef TGTYPES_H
#define TGTYPES_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

/// A peer is addressed by its kind and id; users and channels also need the access hash.
struct TgPeer
{
    enum Kind { User, Chat, Channel };
    TgPeer() : kind(User), id(0), accessHash(0) {}
    TgPeer(Kind k, qint64 i, qint64 h = 0) : kind(k), id(i), accessHash(h) {}
    Kind kind;
    qint64 id;
    qint64 accessHash;

    bool isNull() const { return id == 0; }
    bool isGroup() const { return kind != User; }
    /// "user:123" - the string QML and the models key on.
    QString key() const;
    static TgPeer fromKey(const QString &key);
    bool operator==(const TgPeer &o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const TgPeer &o) const { return !(*this == o); }
};

/// A user or a chat, as far as the responses have said so far.
struct TgPeerInfo
{
    TgPeerInfo() : isBot(false), isSelf(false), isDeleted(false), isContact(false), online(false), lastSeen(0), statusKind(0), membersCount(0), isBroadcast(false), photoId(0), photoDcId(0) {}
    TgPeer peer;
    QString title;         // display name or chat title
    QString firstName, lastName, username, phone;
    bool isBot, isSelf, isDeleted, isContact;
    bool online;
    int lastSeen;          // unix time, or 0
    int statusKind;        // 0 unknown, 1 online, 2 offline with lastSeen, 3 recently, 4 last week, 5 last month, 6 long ago
    int membersCount;
    bool isBroadcast;      // a channel (not a megagroup)
    qint64 photoId;        // profile / chat picture, 0 when none
    int photoDcId;
};

/// An attachment: what is needed to fetch it, plus what a text client shows about it.
struct TgMedia
{
    enum Kind { None, Photo, Video, Document, Voice, Audio, Sticker, Gif, Other };
    TgMedia() : kind(None), id(0), accessHash(0), dcId(0), width(0), height(0), fileSize(0), duration(0) {}
    bool isValid() const { return id != 0; }
    Kind kind;
    qint64 id;
    qint64 accessHash;
    QByteArray fileReference;   // short-lived: a stale one fails with FILE_REFERENCE_EXPIRED
    int dcId;
    QString sizeType;           // photos: the size to show ("m"); documents: empty
    QString bigSizeType;        // photos: the size to save
    QString thumbSizeType;      // documents: the thumbnail's size name, if any
    QByteArray strippedThumb;   // a tiny inline preview, when the message carried one
    int width, height;
    qint64 fileSize;
    int duration;
    QString mimeType;
    QString fileName;
};

/// One message, reduced to what a text-only client displays.
struct TgMessage
{
    TgMessage() : id(0), date(0), out(false), fromId(0), replyToId(0), viaBot(false), service(false), mentioned(false), editDate(0) {}
    int id;
    int date;
    bool out;
    qint64 fromId;
    TgPeer peer;
    QString text;
    QString note;          // "photo", "sticker", "forwarded" - set when not plain text
    int replyToId;
    bool viaBot;
    bool service;          // a service message: the note is the whole story
    bool mentioned;
    int editDate;
    QString forwardedFrom;
    TgMedia media;
};

/// A chat in the dialog list.
struct TgDialog
{
    TgDialog() : topMessageId(0), topMessageDate(0), unreadCount(0), readInboxMaxId(0), readOutboxMaxId(0), mutedUntil(0), pinned(false), archived(false), lastOut(false), lastFromId(0) {}
    TgPeer peer;
    int topMessageId;
    int topMessageDate;
    int unreadCount;
    int readInboxMaxId;
    int readOutboxMaxId;
    int mutedUntil;        // 0 = not muted; Telegram expresses "muted" as a time in the future
    bool pinned;
    bool archived;
    QString lastText;      // preview of the newest message
    bool lastOut;
    qint64 lastFromId;
    bool isMuted(int now) const { return mutedUntil > now; }
};

/// One of the user's chat folders (a Telegram "dialog filter"): a rule that selects chats,
/// not a container they are moved into. The Archive is NOT one of these - it is a real
/// separate list addressed by folder id 1.
struct TgFolder
{
    TgFolder() : id(0), listedOnly(false), contacts(false), nonContacts(false), groups(false),
                 broadcasts(false), bots(false), excludeMuted(false), excludeRead(false), excludeArchived(false) {}
    int id;
    QString title;
    bool listedOnly;      // a shared folder: only the chats it names, no category rules
    bool contacts, nonContacts, groups, broadcasts, bots;
    bool excludeMuted, excludeRead, excludeArchived;
    QList<QString> include, exclude, pinned;   // peer keys named individually
    /// Whether a (non-archived) chat belongs in this folder.
    bool contains(const TgDialog &d, const TgPeerInfo &info, int now) const;
};

/// A secret (end-to-end) chat, as the UI sees it.
struct TgSecretChat
{
    TgSecretChat() : id(0), peerUserId(0), state(0), isCreator(false), ttl(0) {}
    int id;
    qint64 peerUserId;
    int state;            // 0 requested-by-me, 1 requested-to-me, 2 ready, 3 discarded
    bool isCreator;
    int ttl;              // self-destruct timer, seconds (0 = off)
    QByteArray keyHash;   // for the verification screen
    /// Peer key like a normal chat, but with a "secret:" scheme so models can tell them apart.
    QString key() const { return QLatin1String("secret:") + QString::number(id); }
};

// A message inside a secret chat (device-local, in memory only). Carries the self-destruct
// timer so the UI can count down and drop it when it expires.
struct TgSecretMsg
{
    TgSecretMsg() : randomId(0), date(0), out(false), fromId(0), ttl(0), expiresAt(0) {}
    qint64 randomId;
    QString text;
    int date;
    bool out;
    qint64 fromId;
    int ttl;          // self-destruct seconds carried by the message (0 = no timer)
    int expiresAt;    // unix time it self-destructs; 0 = timer not started yet
};

/// The server's update sequence position.
struct TgUpdateState
{
    TgUpdateState() : pts(0), qts(0), date(0), seq(0) {}
    int pts, qts, date, seq;
    bool isValid() const { return pts > 0 || date > 0; }
};

Q_DECLARE_METATYPE(TgPeer)
Q_DECLARE_METATYPE(TgMessage)
Q_DECLARE_METATYPE(TgDialog)
Q_DECLARE_METATYPE(QList<TgMessage>)
Q_DECLARE_METATYPE(QList<TgDialog>)
Q_DECLARE_METATYPE(TgFolder)
Q_DECLARE_METATYPE(QList<TgFolder>)
Q_DECLARE_METATYPE(TgSecretChat)
Q_DECLARE_METATYPE(QList<TgSecretChat>)

#endif // TGTYPES_H
