// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Building the secret-schema message bodies that go inside a decryptedMessageLayer, and
// pulling the content back out of a decrypted one. Only the handful of types a text client
// needs (text message, layer notification, TTL, read/delete service actions). Reading uses
// the schema walker (the secret table is loaded into it), so this is mostly builders.
#ifndef SECRETAPI_H
#define SECRETAPI_H

#include "tlobject.h"

#include <QByteArray>
#include <QList>
#include <QString>

/// What a decrypted secret message turned out to be.
struct SecretContent
{
    enum Kind { Text, NotifyLayer, SetTtl, Read, Delete, Typing, FlushHistory, Unsupported };
    SecretContent() : kind(Unsupported), randomId(0), ttl(0), layer(0) {}
    Kind kind;
    qint64 randomId;
    QString text;
    int ttl;
    int layer;
    QList<qint64> randomIds;     // read / delete
};

namespace SecretApi
{
    /// decryptedMessage (layer 73): a plain text message with an optional self-destruct ttl.
    QByteArray textMessage(qint64 randomId, int ttl, const QString &text);
    /// decryptedMessageService carrying decryptedMessageActionNotifyLayer.
    QByteArray notifyLayer(qint64 randomId, int layer);
    /// decryptedMessageService carrying decryptedMessageActionSetMessageTTL.
    QByteArray setTtl(qint64 randomId, int seconds);
    /// decryptedMessageService carrying decryptedMessageActionDeleteMessages.
    QByteArray deleteMessages(qint64 randomId, const QList<qint64> &ids);

    /// Interprets a decrypted body (the content of decryptedMessageLayer.message).
    SecretContent read(const QByteArray &body);
}

#endif // SECRETAPI_H
