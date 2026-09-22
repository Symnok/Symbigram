// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "secretapi.h"
#include "tlconstructors.h"
#include "tlreader.h"
#include "tlwriter.h"

QByteArray SecretApi::textMessage(qint64 randomId, int ttl, const QString &text)
{
    // decryptedMessage#91cc4674 flags:# random_id:long ttl:int message:string ... (flags=0).
    TlWriter w(text.size() * 3 + 32);
    w.writeConstructor(Tl::DecryptedMessage73).writeInt(0).writeLong(randomId).writeInt(ttl).writeString(text);
    return w.toByteArray();
}

QByteArray SecretApi::notifyLayer(qint64 randomId, int layer)
{
    TlWriter w(32);
    w.writeConstructor(Tl::DecryptedMessageService).writeLong(randomId)
     .writeConstructor(Tl::DecryptedMessageActionNotifyLayer).writeInt(layer);
    return w.toByteArray();
}

QByteArray SecretApi::setTtl(qint64 randomId, int seconds)
{
    TlWriter w(32);
    w.writeConstructor(Tl::DecryptedMessageService).writeLong(randomId)
     .writeConstructor(Tl::DecryptedMessageActionSetMessageTTL).writeInt(seconds);
    return w.toByteArray();
}

QByteArray SecretApi::deleteMessages(qint64 randomId, const QList<qint64> &ids)
{
    TlWriter w(32 + ids.size() * 8);
    w.writeConstructor(Tl::DecryptedMessageService).writeLong(randomId)
     .writeConstructor(Tl::DecryptedMessageActionDeleteMessages).writeVectorOfLong(ids);
    return w.toByteArray();
}

SecretContent SecretApi::read(const QByteArray &body)
{
    SecretContent c;
    TlReader r(body);
    TlObject o = TlSchema::readObject(r);
    if (o.ctor() == Tl::DecryptedMessage73) {
        c.kind = SecretContent::Text;
        c.randomId = o.longOr("random_id");
        c.ttl = o.intOr("ttl");
        c.text = o.str("message");
        return c;
    }
    if (o.ctor() == Tl::DecryptedMessageService) {
        c.randomId = o.longOr("random_id");
        TlObject a = o.obj("action");
        switch (a.ctor()) {
        case Tl::DecryptedMessageActionNotifyLayer: c.kind = SecretContent::NotifyLayer; c.layer = a.intOr("layer"); break;
        case Tl::DecryptedMessageActionSetMessageTTL: c.kind = SecretContent::SetTtl; c.ttl = a.intOr("ttl_seconds"); break;
        case Tl::DecryptedMessageActionFlushHistory: c.kind = SecretContent::FlushHistory; break;
        case Tl::DecryptedMessageActionTyping: c.kind = SecretContent::Typing; break;
        case Tl::DecryptedMessageActionReadMessages: {
            c.kind = SecretContent::Read;
            QVariantList v = a.vec("random_ids");
            for (int i = 0; i < v.size(); ++i) c.randomIds.append(v.at(i).toLongLong());
            break;
        }
        case Tl::DecryptedMessageActionDeleteMessages: {
            c.kind = SecretContent::Delete;
            QVariantList v = a.vec("random_ids");
            for (int i = 0; i < v.size(); ++i) c.randomIds.append(v.at(i).toLongLong());
            break;
        }
        default: c.kind = SecretContent::Unsupported; break;
        }
        return c;
    }
    return c;
}
