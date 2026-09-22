// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Encrypts and decrypts messages under an established authorisation key - MTProto 2.0's
// message layer:
//     msg_key = middle 128 bits of SHA256(auth_key[88+x .. +32] || plaintext)
//     aes_key / aes_iv derived from msg_key and the auth key by SHA256
//     payload encrypted with AES-256 IGE
// x is 0 for messages we send and 8 for messages we receive. Decryption re-derives msg_key
// from the decrypted plaintext and compares; because IGE propagates any change through the
// rest of the message, that check is what makes tampering detectable.
#ifndef MTPROTOSESSION_H
#define MTPROTOSESSION_H

#include "authkeyhandshake.h"

#include <QByteArray>

class MtprotoSession
{
public:
    explicit MtprotoSession(const AuthKey &authKey);

    qint64 sessionId() const { return m_sessionId; }
    qint64 serverSalt() const { return m_serverSalt; }
    void setServerSalt(qint64 salt) { m_serverSalt = salt; }
    int timeOffset() const { return m_timeOffset; }
    const AuthKey &authKey() const { return m_authKey; }

    /// How far the server's clock is ahead of the one this session is using, in seconds.
    int driftFrom(int serverUnixTime) const;
    /// Re-bases message ids on the server's clock (bad_msg_notification 16/17).
    void syncTime(int serverUnixTime);
    /// Starts a fresh session, keeping the key (bad_msg_notification 32/33).
    void renew();

    /// Wraps a TL body in an encrypted message. contentRelated controls the sequence
    /// number: real API calls consume one, bare service messages like pings do not.
    QByteArray encrypt(const QByteArray &body, bool contentRelated, qint64 &msgId);
    /// Unwraps an encrypted message; throws TlException on any inconsistency.
    QByteArray decrypt(const QByteArray &packet, qint64 &msgId, int &seqNo);

private:
    QByteArray computeMsgKey(const QByteArray &plaintext, int x) const;
    void deriveKeys(const QByteArray &msgKey, int x, QByteArray &aesKey, QByteArray &aesIv) const;
    qint64 nextMessageId();
    int nextSeqNo(bool contentRelated);

    AuthKey m_authKey;
    qint64 m_sessionId;
    qint64 m_serverSalt;
    int m_timeOffset;
    qint64 m_lastMsgId;
    int m_seqNo;
};

#endif // MTPROTOSESSION_H
