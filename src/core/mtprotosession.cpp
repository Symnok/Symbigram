// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "mtprotosession.h"
#include "crypto.h"
#include "tlreader.h"
#include "tlwriter.h"

#include <QDateTime>

namespace
{
    int unixNow() { return int(QDateTime::currentDateTime().toTime_t()); }
}

MtprotoSession::MtprotoSession(const AuthKey &authKey)
    : m_authKey(authKey), m_serverSalt(authKey.serverSalt), m_timeOffset(authKey.timeOffset), m_lastMsgId(0), m_seqNo(0)
{
    m_sessionId = qint64(Crypto::randomUInt64());
}

int MtprotoSession::driftFrom(int serverUnixTime) const
{
    return serverUnixTime - (unixNow() + m_timeOffset);
}

void MtprotoSession::syncTime(int serverUnixTime)
{
    m_timeOffset = serverUnixTime - unixNow();
    // When the clock was running ahead the new ids are lower; the old high-water mark
    // would keep producing exactly the ids the server just refused.
    m_lastMsgId = 0;
}

void MtprotoSession::renew()
{
    m_sessionId = qint64(Crypto::randomUInt64());
    m_lastMsgId = 0;
    m_seqNo = 0;
}

qint64 MtprotoSession::nextMessageId()
{
    qint64 now = unixNow() + m_timeOffset;
    qint64 id = (now << 32) | ((qint64(uchar(Crypto::randomBytes(1).at(0))) << 8) & 0xFFFC);
    if (id <= m_lastMsgId) id = m_lastMsgId + 4;
    m_lastMsgId = id;
    return id;
}

int MtprotoSession::nextSeqNo(bool contentRelated)
{
    int seq = m_seqNo * 2 + (contentRelated ? 1 : 0);
    if (contentRelated) ++m_seqNo;
    return seq;
}

QByteArray MtprotoSession::computeMsgKey(const QByteArray &plaintext, int x) const
{
    return Crypto::sha256(m_authKey.key.mid(88 + x, 32) + plaintext).mid(8, 16);
}

void MtprotoSession::deriveKeys(const QByteArray &msgKey, int x, QByteArray &aesKey, QByteArray &aesIv) const
{
    QByteArray a = Crypto::sha256(msgKey + m_authKey.key.mid(x, 36));
    QByteArray b = Crypto::sha256(m_authKey.key.mid(40 + x, 36) + msgKey);
    aesKey = a.left(8) + b.mid(8, 16) + a.mid(24, 8);
    aesIv = b.left(8) + a.mid(8, 16) + b.mid(24, 8);
}

QByteArray MtprotoSession::encrypt(const QByteArray &body, bool contentRelated, qint64 &msgId)
{
    msgId = nextMessageId();
    int seq = nextSeqNo(contentRelated);

    TlWriter plain(body.size() + 64);
    plain.writeLong(m_serverSalt).writeLong(m_sessionId).writeLong(msgId).writeInt(seq).writeInt(body.size()).writeRaw(body);
    // 12..1024 bytes of padding, total length a multiple of 16.
    int pad = 16 - (plain.length() % 16);
    if (pad < 12) pad += 16;
    plain.writeRaw(Crypto::randomBytes(pad));

    QByteArray plaintext = plain.toByteArray();
    QByteArray msgKey = computeMsgKey(plaintext, 0);
    QByteArray aesKey, aesIv;
    deriveKeys(msgKey, 0, aesKey, aesIv);
    QByteArray encrypted = Crypto::aesIgeEncrypt(plaintext, aesKey, aesIv);

    TlWriter packet(24 + encrypted.size());
    packet.writeLong(m_authKey.keyId).writeRaw(msgKey).writeRaw(encrypted);
    return packet.toByteArray();
}

QByteArray MtprotoSession::decrypt(const QByteArray &packet, qint64 &msgId, int &seqNo)
{
    if (packet.size() < 24) throw TlException(QLatin1String("encrypted message shorter than its header"));
    TlReader r(packet);
    qint64 keyId = r.readLong();
    if (keyId != m_authKey.keyId)
        throw TlException(QString::fromLatin1("message encrypted to key %1, ours is %2")
                          .arg(quint64(keyId), 16, 16, QLatin1Char('0')).arg(quint64(m_authKey.keyId), 16, 16, QLatin1Char('0')));
    QByteArray msgKey = r.readRaw(16);
    QByteArray encrypted = r.readRaw(packet.size() - 24);
    if (encrypted.size() % 16 != 0) throw TlException(QLatin1String("encrypted payload is not a whole number of blocks"));

    QByteArray aesKey, aesIv;
    deriveKeys(msgKey, 8, aesKey, aesIv);        // x = 8 for server messages
    QByteArray plaintext = Crypto::aesIgeDecrypt(encrypted, aesKey, aesIv);
    if (!Crypto::constantTimeEquals(msgKey, computeMsgKey(plaintext, 8)))
        throw TlException(QLatin1String("msg_key mismatch - message was corrupted or forged"));

    TlReader pr(plaintext);
    pr.readLong();                               // salt
    qint64 session = pr.readLong();
    if (session != m_sessionId) throw TlException(QLatin1String("message belongs to a different session"));
    msgId = pr.readLong();
    seqNo = pr.readInt();
    int length = pr.readInt();
    if (length < 0 || length > plaintext.size() - 32)
        throw TlException(QString::fromLatin1("declared body length %1 does not fit the message").arg(length));
    return pr.readRaw(length);
}
