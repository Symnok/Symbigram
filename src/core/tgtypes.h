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
    TgPeerInfo() : isBot(false), isSelf(false), isDeleted(false), isContact(false), online(false), lastSeen(0), statusKind(0), membersCount(0), isBroadcast(false) {}
    TgPeer peer;
    QString title;         // display name or chat title
    QString firstName, lastName, username, phone;
    bool isBot, isSelf, isDeleted, isContact;
    bool online;
    int lastSeen;          // unix time, or 0
    int statusKind;        // 0 unknown, 1 online, 2 offline with lastSeen, 3 recently, 4 last week, 5 last month, 6 long ago
    int membersCount;
    bool isBroadcast;      // a channel (not a megagroup)
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
};

/// A chat in the dialog list.
struct TgDialog
{
    TgDialog() : topMessageId(0), topMessageDate(0), unreadCount(0), readInboxMaxId(0), readOutboxMaxId(0), mutedUntil(0), pinned(false), lastOut(false), lastFromId(0) {}
    TgPeer peer;
    int topMessageId;
    int topMessageDate;
    int unreadCount;
    int readInboxMaxId;
    int readOutboxMaxId;
    int mutedUntil;        // 0 = not muted; Telegram expresses "muted" as a time in the future
    bool pinned;
    QString lastText;      // preview of the newest message
    bool lastOut;
    qint64 lastFromId;
    bool isMuted(int now) const { return mutedUntil > now; }
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

#endif // TGTYPES_H
