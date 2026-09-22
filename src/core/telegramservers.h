// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Telegram's server public keys and datacenter addresses - the only constants a client
// needs to bootstrap; everything else is negotiated. The keys and addresses are the ones
// LumigramPlus verified against live Telegram (originally from the Unigram sources).
#ifndef TELEGRAMSERVERS_H
#define TELEGRAMSERVERS_H

#include "bigint.h"

#include <QByteArray>
#include <QList>
#include <QString>

/// One of Telegram's server public keys. MTProto encrypts a 255-byte block with raw
/// textbook exponentiation, so x^e mod n over BigInt is the entire RSA operation. The
/// fingerprint is derived (low 64 bits of SHA1 over the TL serialisation of modulus and
/// exponent) rather than hardcoded, so key material and fingerprint cannot drift apart.
class RsaKey
{
public:
    RsaKey() : m_fingerprint(0) {}
    RsaKey(const QByteArray &modulusBE, const QByteArray &exponentBE);
    /// Parses a PKCS#1 "RSA PUBLIC KEY" PEM - SEQUENCE { INTEGER n, INTEGER e }.
    static RsaKey fromPem(const QString &pem);

    qint64 fingerprint() const { return m_fingerprint; }
    /// Raw RSA over a block shorter than the modulus; 255 bytes in, 256 out.
    QByteArray encrypt(const QByteArray &data) const;

private:
    BigInt m_modulus, m_exponent;
    qint64 m_fingerprint;
};

namespace TelegramServers
{
    const int DefaultPort = 443;
    QList<RsaKey> publicKeys();
    /// Picks the key matching one of the fingerprints the server offered; null-fingerprint
    /// key when none does.
    RsaKey findByFingerprint(const QList<qint64> &offered);
    /// The address of a production datacenter by its id (1..5).
    QString hostFor(int dcId);
    const int DefaultDc = 2;
}

#endif // TELEGRAMSERVERS_H
