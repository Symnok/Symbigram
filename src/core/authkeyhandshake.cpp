// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "authkeyhandshake.h"
#include "crypto.h"
#include "dh.h"
#include "mtprototransport.h"
#include "telegramservers.h"
#include "tlconstructors.h"
#include "tlreader.h"
#include "tlwriter.h"

#include <QDateTime>

namespace
{
    int unixNow() { return int(QDateTime::currentDateTime().toTime_t()); }

    quint64 toUInt64BE(const QByteArray &b)
    {
        quint64 v = 0;
        for (int i = 0; i < b.size(); ++i) v = (v << 8) | uchar(b.at(i));
        return v;
    }

    QByteArray fromUInt32BE(quint32 v)
    {
        QByteArray b(4, '\0');
        b[0] = char(v >> 24); b[1] = char(v >> 16); b[2] = char(v >> 8); b[3] = char(v);
        return b;
    }

    qint64 toInt64LE(const QByteArray &b, int offset)
    {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i) v |= quint64(uchar(b.at(offset + i))) << (8 * i);
        return qint64(v);
    }
}

// -- worker --------------------------------------------------------------------------------------------

void DhWorker::run()
{
    try {
        BigInt p = BigInt::fromBytesBE(dhPrime);
        BigInt gaBig = BigInt::fromBytesBE(ga);
        DhValidation::validateParameters(g, p, gaBig, &note);
        BigInt bBig = BigInt::fromBytesBE(b);
        BigInt gbBig = BigInt::modPow(BigInt::fromUInt(quint32(g)), bBig, p);
        DhValidation::validatePublicValue(gbBig, p, "g_b");
        gb = gbBig.toBytesBE(256);
        authKey = BigInt::modPow(gaBig, bBig, p).toBytesBE(256);
    } catch (const TlException &e) {
        error = e.message();
    }
}

// -- handshake -------------------------------------------------------------------------------------------

AuthKeyHandshake::AuthKeyHandshake(MtprotoTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport), m_worker(0), m_stage(Idle), m_lastMsgId(0), m_timeOffset(0)
{
}

AuthKeyHandshake::~AuthKeyHandshake()
{
    if (m_worker) {
        m_worker->wait();
        delete m_worker;
    }
}

qint64 AuthKeyHandshake::nextMessageId()
{
    // Unix time in the high 32 bits, a counter in the low ones, divisible by 4, strictly
    // increasing within a session.
    qint64 id = (qint64(unixNow()) << 32) | ((qint64(uchar(Crypto::randomBytes(1).at(0))) << 8) & 0xFFFC);
    if (id <= m_lastMsgId) id = m_lastMsgId + 4;
    m_lastMsgId = id;
    return id;
}

void AuthKeyHandshake::send(const QByteArray &body)
{
    // Handshake traffic is unencrypted: auth_key_id = 0, the key does not exist yet.
    TlWriter packet(body.size() + 20);
    packet.writeLong(0).writeLong(nextMessageId()).writeInt(body.size()).writeRaw(body);
    m_transport->sendPacket(packet.toByteArray());
}

void AuthKeyHandshake::fail(const QString &reason)
{
    m_stage = Done;
    emit failed(reason);
}

void AuthKeyHandshake::start()
{
    m_nonce = Crypto::randomBytes(16);
    TlWriter w;
    w.writeConstructor(Tl::ReqPQ).writeRaw(m_nonce);
    m_stage = SentReqPq;
    emit log(QLatin1String("-> req_pq"));
    send(w.toByteArray());
}

void AuthKeyHandshake::onPacket(const QByteArray &packet)
{
    if (m_stage != SentReqPq && m_stage != SentReqDh && m_stage != SentSetClientDh) return;
    try {
        TlReader hdr(packet);
        qint64 keyId = hdr.readLong();
        if (keyId != 0) throw TlException(QString::fromLatin1("expected an unencrypted reply, got key id %1").arg(keyId));
        hdr.readLong();                         // message id
        int length = hdr.readInt();
        if (length < 0 || length > hdr.remaining()) throw TlException(QLatin1String("declared body length exceeds the packet"));
        TlReader r(packet, hdr.position());

        if (m_stage == SentReqPq) {
            r.expect(Tl::ResPQ, "resPQ");
            QByteArray echoNonce = r.readRaw(16);
            m_serverNonce = r.readRaw(16);
            QByteArray pqBytes = r.readBytes();
            QList<qint64> fingerprints = r.readVectorOfLong();
            if (!Crypto::constantTimeEquals(m_nonce, echoNonce)) throw TlException(QLatin1String("resPQ echoed a different nonce"));

            quint64 pq = toUInt64BE(pqBytes);
            quint64 p, q;
            PqFactorization::factor(pq, p, q);
            emit log(QString::fromLatin1("<- resPQ  pq=%1 = %2 * %3").arg(pq).arg(p).arg(q));

            RsaKey rsaKey = TelegramServers::findByFingerprint(fingerprints);
            if (rsaKey.fingerprint() == 0) throw TlException(QLatin1String("server offered no public key we recognise"));

            m_newNonce = Crypto::randomBytes(32);
            QByteArray pBytes = fromUInt32BE(quint32(p)), qBytes = fromUInt32BE(quint32(q));
            TlWriter inner;
            inner.writeConstructor(Tl::PQInnerData).writeBytes(pqBytes).writeBytes(pBytes).writeBytes(qBytes)
                 .writeRaw(m_nonce).writeRaw(m_serverNonce).writeRaw(m_newNonce);
            QByteArray innerData = inner.toByteArray();
            // SHA1 + data + random padding, to exactly 255 bytes so the value stays below
            // the 2048-bit modulus.
            QByteArray hashed = Crypto::sha1(innerData) + innerData;
            if (hashed.size() > 255) throw TlException(QLatin1String("p_q_inner_data too large"));
            QByteArray padded = hashed + Crypto::randomBytes(255 - hashed.size());
            QByteArray encryptedInner = rsaKey.encrypt(padded);

            TlWriter w;
            w.writeConstructor(Tl::ReqDHParams).writeRaw(m_nonce).writeRaw(m_serverNonce)
             .writeBytes(pBytes).writeBytes(qBytes).writeLong(rsaKey.fingerprint()).writeBytes(encryptedInner);
            m_stage = SentReqDh;
            emit log(QString::fromLatin1("-> req_DH_params  key=%1").arg(quint64(rsaKey.fingerprint()), 16, 16, QLatin1Char('0')));
            send(w.toByteArray());
            return;
        }

        if (m_stage == SentReqDh) {
            quint32 answerType = r.readConstructor();
            if (answerType == Tl::ServerDHParamsFail) throw TlException(QLatin1String("server rejected DH params (server_DH_params_fail)"));
            if (answerType != Tl::ServerDHParamsOk)
                throw TlException(QString::fromLatin1("unexpected reply 0x%1").arg(answerType, 8, 16, QLatin1Char('0')));
            r.readRaw(16);                                  // nonce
            QByteArray serverNonceEcho = r.readRaw(16);
            QByteArray encryptedAnswer = r.readBytes();
            if (!Crypto::constantTimeEquals(m_serverNonce, serverNonceEcho))
                throw TlException(QLatin1String("server_DH_params_ok echoed a different server_nonce"));

            // tmp_aes_key and tmp_aes_iv, from overlapping slices of three SHA1 digests.
            QByteArray nsHash = Crypto::sha1(m_newNonce + m_serverNonce);
            QByteArray snHash = Crypto::sha1(m_serverNonce + m_newNonce);
            QByteArray nnHash = Crypto::sha1(m_newNonce + m_newNonce);
            m_tmpKey = nsHash + snHash.left(12);
            m_tmpIv = snHash.mid(12, 8) + nnHash + m_newNonce.left(4);

            QByteArray answerWithHash = Crypto::aesIgeDecrypt(encryptedAnswer, m_tmpKey, m_tmpIv);
            TlReader ar(answerWithHash, 20);                // skip the leading SHA1
            ar.expect(Tl::ServerDHInnerData, "server_DH_inner_data");
            ar.readRaw(16);                                 // nonce
            ar.readRaw(16);                                 // server_nonce
            int g = ar.readInt();
            QByteArray dhPrimeBytes = ar.readBytes();
            QByteArray gaBytes = ar.readBytes();
            int serverTime = ar.readInt();
            // The hash covers only the message, not the padding after it.
            int answerLength = ar.position() - 20;
            if (!Crypto::constantTimeEquals(answerWithHash.left(20), Crypto::sha1(answerWithHash.mid(20, answerLength))))
                throw TlException(QLatin1String("server_DH_inner_data failed its hash check"));
            m_timeOffset = serverTime - unixNow();
            emit log(QString::fromLatin1("<- server_DH_inner_data  g=%1 prime=%2 bits time offset=%3s").arg(g).arg(dhPrimeBytes.size() * 8).arg(m_timeOffset));

            m_worker = new DhWorker(this);
            m_worker->g = g;
            m_worker->dhPrime = dhPrimeBytes;
            m_worker->ga = gaBytes;
            m_worker->b = Crypto::randomBytes(256);
            connect(m_worker, SIGNAL(finished()), this, SLOT(onWorkerDone()));
            m_stage = Computing;
            m_worker->start();
            return;
        }

        if (m_stage == SentSetClientDh) {
            quint32 genResult = r.readConstructor();
            r.readRaw(16);                                  // nonce
            r.readRaw(16);                                  // server_nonce
            QByteArray newNonceHash = r.readRaw(16);
            if (genResult == Tl::DhGenRetry) throw TlException(QLatin1String("server asked to retry DH generation"));
            if (genResult == Tl::DhGenFail) throw TlException(QLatin1String("server reported dh_gen_fail"));
            if (genResult != Tl::DhGenOk)
                throw TlException(QString::fromLatin1("unexpected DH result 0x%1").arg(genResult, 8, 16, QLatin1Char('0')));

            QByteArray authKeySha = Crypto::sha1(m_worker->authKey);
            QByteArray auxHash = authKeySha.left(8);
            QByteArray expected = Crypto::sha1(m_newNonce + QByteArray(1, '\x01') + auxHash).mid(4, 16);
            if (!Crypto::constantTimeEquals(newNonceHash, expected))
                throw TlException(QLatin1String("new_nonce_hash1 mismatch - the server derived a different key"));

            AuthKey key;
            key.key = m_worker->authKey;
            key.keyId = toInt64LE(authKeySha, 12);
            key.serverSalt = toInt64LE(m_newNonce, 0) ^ toInt64LE(m_serverNonce, 0);
            key.timeOffset = m_timeOffset;
            emit log(QString::fromLatin1("<- dh_gen_ok  auth_key_id=%1").arg(quint64(key.keyId), 16, 16, QLatin1Char('0')));
            m_stage = Done;
            emit finished(key);
            return;
        }
    } catch (const TlException &e) {
        fail(e.message());
    }
}

void AuthKeyHandshake::onWorkerDone()
{
    if (m_stage != Computing) return;
    if (!m_worker->error.isEmpty()) { fail(m_worker->error); return; }
    emit log(QLatin1String("   ") + m_worker->note);
    try {
        TlWriter clientInner;
        clientInner.writeConstructor(Tl::ClientDHInnerData).writeRaw(m_nonce).writeRaw(m_serverNonce)
                   .writeLong(0)                            // retry_id
                   .writeBytes(m_worker->gb);
        QByteArray clientInnerData = clientInner.toByteArray();
        QByteArray clientHashed = Crypto::sha1(clientInnerData) + clientInnerData;
        int padLen = (16 - (clientHashed.size() % 16)) % 16;
        QByteArray clientPadded = clientHashed + Crypto::randomBytes(padLen);
        QByteArray encryptedClient = Crypto::aesIgeEncrypt(clientPadded, m_tmpKey, m_tmpIv);

        TlWriter w;
        w.writeConstructor(Tl::SetClientDHParams).writeRaw(m_nonce).writeRaw(m_serverNonce).writeBytes(encryptedClient);
        m_stage = SentSetClientDh;
        emit log(QLatin1String("-> set_client_DH_params"));
        send(w.toByteArray());
    } catch (const TlException &e) {
        fail(e.message());
    }
}
