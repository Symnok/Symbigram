// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "secretchat.h"
#include "bigint.h"
#include "crypto.h"
#include "dh.h"
#include "tlconstructors.h"
#include "tlreader.h"
#include "tlwriter.h"

namespace
{
    qint64 toInt64LE(const QByteArray &b, int offset)
    {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i) v |= quint64(uchar(b.at(offset + i))) << (8 * i);
        return qint64(v);
    }
}

SecretChat::SecretChat()
    : m_chatId(0), m_accessHash(0), m_peerUserId(0), m_adminId(0), m_participantId(0), m_isCreator(false),
      m_state(Discarded), m_requestRandomId(0), m_keyFingerprint(0), m_ttl(0), m_outSeqCount(0), m_inSeqCount(0),
      m_layer(Tl::SecretLayer)
{
}

qint64 SecretChat::fingerprintOf(const QByteArray &key)
{
    // The low 64 bits of SHA1(key) - the same shape as an auth_key_id.
    QByteArray sha = Crypto::sha1(key);
    return toInt64LE(sha, 12);
}

QByteArray SecretChat::computeKey(const QByteArray &otherPublic, const QByteArray &myScalar, const QByteArray &p) const
{
    BigInt prime = BigInt::fromBytesBE(p);
    BigInt other = BigInt::fromBytesBE(otherPublic);
    BigInt s = BigInt::fromBytesBE(myScalar);
    return BigInt::modPow(other, s, prime).toBytesBE(256);
}

QByteArray SecretChat::startAsCreator(qint64 peerUserId, int requestRandomId, int g, const QByteArray &p)
{
    m_isCreator = true;
    m_peerUserId = peerUserId;
    m_requestRandomId = requestRandomId;
    m_p = p;
    m_state = RequestedByMe;
    BigInt prime = BigInt::fromBytesBE(p);
    // g_a = g^a mod p, with a a fresh 256-byte secret and g_a bounds-checked.
    while (true) {
        m_myScalar = Crypto::randomBytes(256);
        BigInt ga = BigInt::modPow(BigInt::fromUInt(quint32(g)), BigInt::fromBytesBE(m_myScalar), prime);
        try {
            DhValidation::validatePublicValue(ga, prime, "g_a");
            return ga.toBytesBE(256);
        } catch (const TlException &) {
            // pick another a
        }
    }
}

void SecretChat::setIncomingRequest(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId, const QByteArray &gA)
{
    m_isCreator = false;
    m_chatId = chatId;
    m_accessHash = accessHash;
    m_adminId = adminId;
    m_participantId = participantId;
    m_peerUserId = adminId;
    m_incomingGa = gA;
    m_state = RequestedToMe;
}

QByteArray SecretChat::acceptAsParticipant(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId,
                                           const QByteArray &gA, int g, const QByteArray &p)
{
    m_isCreator = false;
    m_chatId = chatId;
    m_accessHash = accessHash;
    m_adminId = adminId;
    m_participantId = participantId;
    m_peerUserId = adminId;                 // the other party is the creator
    m_p = p;
    BigInt prime = BigInt::fromBytesBE(p);
    BigInt gaBig = BigInt::fromBytesBE(gA);
    DhValidation::validatePublicValue(gaBig, prime, "g_a");
    while (true) {
        m_myScalar = Crypto::randomBytes(256);
        BigInt gb = BigInt::modPow(BigInt::fromUInt(quint32(g)), BigInt::fromBytesBE(m_myScalar), prime);
        try {
            DhValidation::validatePublicValue(gb, prime, "g_b");
            m_key = computeKey(gA, m_myScalar, p);
            m_keyFingerprint = fingerprintOf(m_key);
            m_state = Ready;
            m_myScalar.clear();
            m_p.clear();
            return gb.toBytesBE(256);
        } catch (const TlException &) {
        }
    }
}

bool SecretChat::finishAsCreator(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId,
                                 const QByteArray &gB, qint64 fingerprint)
{
    m_chatId = chatId;
    m_accessHash = accessHash;
    m_adminId = adminId;
    m_participantId = participantId;
    BigInt prime = BigInt::fromBytesBE(m_p);
    BigInt gbBig = BigInt::fromBytesBE(gB);
    DhValidation::validatePublicValue(gbBig, prime, "g_b");
    m_key = computeKey(gB, m_myScalar, m_p);
    m_keyFingerprint = fingerprintOf(m_key);
    m_myScalar.clear();
    m_p.clear();
    if (m_keyFingerprint != fingerprint) { m_state = Discarded; return false; }
    m_state = Ready;
    return true;
}

QByteArray SecretChat::keyHash() const
{
    // What the two sides compare: SHA1(key)[0:16] followed by SHA256(key)[0:20] on modern
    // layers. Layer 73 uses the first 16 bytes of SHA1(key) plus SHA256; we return a stable
    // digest of the key for the verification screen.
    return Crypto::sha256(m_key);
}

// -- message encryption ----------------------------------------------------------------------------------

QByteArray SecretChat::msgKey(const QByteArray &plaintext, int x) const
{
    return Crypto::sha256(m_key.mid(88 + x, 32) + plaintext).mid(8, 16);
}

void SecretChat::deriveKeys(const QByteArray &mk, int x, QByteArray &aesKey, QByteArray &aesIv) const
{
    // The MTProto 2.0 key derivation, identical to the client-server session.
    QByteArray a = Crypto::sha256(mk + m_key.mid(x, 36));
    QByteArray b = Crypto::sha256(m_key.mid(40 + x, 36) + mk);
    aesKey = a.left(8) + b.mid(8, 16) + a.mid(24, 8);
    aesIv = b.left(8) + a.mid(8, 16) + b.mid(24, 8);
}

QByteArray SecretChat::encryptMessage(const QByteArray &decryptedMessageBody)
{
    // Wrap in decryptedMessageLayer with the seq numbers, then MTProto-2.0 encrypt.
    // Parity per Telegram's spec (core.telegram.org/api/end-to-end/seq_no): an outgoing
    // message carries parity 1 for the chat creator and 0 for the other party, and
    // in_seq_no carries the opposite parity. The official apps validate this and terminate
    // the chat (or silently drop the message) on a parity violation, so it must be exact.
    const int outX = m_isCreator ? 1 : 0;
    const int inX = m_isCreator ? 0 : 1;
    const int outSeq = m_outSeqCount * 2 + outX;
    const int inSeq = m_inSeqCount * 2 + inX;

    TlWriter layer(decryptedMessageBody.size() + 48);
    layer.writeConstructor(Tl::DecryptedMessageLayer)
         .writeBytes(Crypto::randomBytes(16))
         .writeInt(m_layer)
         .writeInt(inSeq)
         .writeInt(outSeq)
         .writeRaw(decryptedMessageBody);
    QByteArray body = layer.toByteArray();

    // to_encrypt = int32(len) + body + padding (12..1024, total % 16 == 0).
    TlWriter plain(body.size() + 32);
    plain.writeInt(body.size()).writeRaw(body);
    int pad = 16 - (plain.length() % 16);
    if (pad < 12) pad += 16;
    plain.writeRaw(Crypto::randomBytes(pad));
    QByteArray plaintext = plain.toByteArray();

    QByteArray mk = msgKey(plaintext, m_isCreator ? 0 : 8);   // x=0 for the creator's own messages
    QByteArray aesKey, aesIv;
    deriveKeys(mk, m_isCreator ? 0 : 8, aesKey, aesIv);
    QByteArray encrypted = Crypto::aesIgeEncrypt(plaintext, aesKey, aesIv);

    TlWriter out(encrypted.size() + 24);
    out.writeLong(m_keyFingerprint).writeRaw(mk).writeRaw(encrypted);
    ++m_outSeqCount;
    return out.toByteArray();
}

QByteArray SecretChat::decryptMessage(const QByteArray &bytes, int &senderOutSeqNo)
{
    senderOutSeqNo = -1;
    if (bytes.size() < 24) throw TlException(QLatin1String("secret message shorter than its header"));
    TlReader r(bytes);
    qint64 fp = r.readLong();
    if (fp != m_keyFingerprint)
        throw TlException(QLatin1String("secret message encrypted to a different key"));
    QByteArray mk = r.readRaw(16);
    QByteArray encrypted = r.readRaw(bytes.size() - 24);
    if (encrypted.size() % 16 != 0) throw TlException(QLatin1String("secret payload not a whole number of blocks"));

    // The sender is the other party, so x is the opposite of ours.
    const int x = m_isCreator ? 8 : 0;
    QByteArray aesKey, aesIv;
    deriveKeys(mk, x, aesKey, aesIv);
    QByteArray plaintext = Crypto::aesIgeDecrypt(encrypted, aesKey, aesIv);
    if (!Crypto::constantTimeEquals(mk, msgKey(plaintext, x)))
        throw TlException(QLatin1String("secret msg_key mismatch - corrupted or forged"));

    TlReader pr(plaintext);
    int len = pr.readInt();
    if (len < 0 || len > plaintext.size() - 4) throw TlException(QLatin1String("secret message length out of range"));
    QByteArray body = pr.readRaw(len);

    TlReader lr(body);
    if (lr.readConstructor() != Tl::DecryptedMessageLayer)
        throw TlException(QLatin1String("secret payload is not a decryptedMessageLayer"));
    lr.readBytes();                          // random_bytes
    int peerLayer = lr.readInt();
    lr.readInt();                            // in_seq_no (their view of us)
    senderOutSeqNo = lr.readInt();
    if (peerLayer < m_layer) m_layer = peerLayer;   // settle on the lower of the two layers
    ++m_inSeqCount;
    // Return the DecryptedMessage that follows (constructor + fields).
    return body.mid(lr.position());
}

// -- persistence -----------------------------------------------------------------------------------------

void SecretChat::save(QDataStream &s) const
{
    s << qint32(m_chatId) << m_accessHash << m_peerUserId << m_adminId << m_participantId
      << quint8(m_isCreator ? 1 : 0) << qint32(int(m_state)) << qint32(m_requestRandomId)
      << m_myScalar << m_p << m_incomingGa << m_key << m_keyFingerprint << qint32(m_ttl)
      << qint32(m_outSeqCount) << qint32(m_inSeqCount) << qint32(m_layer);
}

bool SecretChat::load(QDataStream &s)
{
    quint8 creator = 0;
    qint32 chatId, st, rr, ttl, outc, inc, layer;
    s >> chatId >> m_accessHash >> m_peerUserId >> m_adminId >> m_participantId >> creator >> st >> rr
      >> m_myScalar >> m_p >> m_incomingGa >> m_key >> m_keyFingerprint >> ttl >> outc >> inc >> layer;
    if (s.status() != QDataStream::Ok) return false;
    m_chatId = chatId;
    m_isCreator = creator != 0;
    m_state = State(st);
    m_requestRandomId = rr;
    m_ttl = ttl;
    m_outSeqCount = outc;
    m_inSeqCount = inc;
    m_layer = layer;
    return true;
}
