// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The primitives MTProto needs and Qt 4.7 does not provide: SHA-256, SHA-512 (with HMAC
// and PBKDF2 for two-step verification), AES-256 in IGE mode, and secure random bytes.
// SHA-1 comes from QCryptographicHash. Everything is plain C++ so the same code runs on
// the phone and in the desktop harness; Symbian's OpenSSL would be faster but is a second
// code path that cannot be tested on the PC.
#ifndef CRYPTO_H
#define CRYPTO_H

#include <QByteArray>

namespace Crypto
{
    QByteArray sha1(const QByteArray &data);
    QByteArray sha256(const QByteArray &data);
    QByteArray sha512(const QByteArray &data);
    QByteArray hmacSha512(const QByteArray &key, const QByteArray &data);
    /// PBKDF2-HMAC-SHA512 (RFC 2898).
    QByteArray pbkdf2Sha512(const QByteArray &password, const QByteArray &salt, int iterations, int outputLength);

    /// Cryptographically secure random bytes: nonces and the DH secret depend on it.
    QByteArray randomBytes(int count);
    quint64 randomUInt64();

    /// AES-256 IGE: 32-byte key, 32-byte IV, data a multiple of 16 bytes.
    QByteArray aesIgeEncrypt(const QByteArray &data, const QByteArray &key32, const QByteArray &iv32);
    QByteArray aesIgeDecrypt(const QByteArray &data, const QByteArray &key32, const QByteArray &iv32);
    /// One 16-byte block, for the self test.
    QByteArray aesEncryptBlock(const QByteArray &key32, const QByteArray &block16);
    QByteArray aesDecryptBlock(const QByteArray &key32, const QByteArray &block16);

    /// Length-independent comparison, for anything an attacker can probe.
    bool constantTimeEquals(const QByteArray &a, const QByteArray &b);
}

/// AES-256 in CTR mode, as one continuous keystream across many calls - what an MTProto proxy
/// wraps the whole TCP stream in (see mtprototransport.cpp). Deliberately stateful: the counter
/// and the position inside the current keystream block carry over, so a packet boundary is not a
/// block boundary. One object per direction per connection; not copyable, and not thread-safe.
///
/// CTR needs only the forward direction of the cipher, which is why encrypting and decrypting are
/// the same call. The key schedule is built once, in the constructor's wake, rather than per
/// block: the stream covers every byte on the wire, including file downloads.
class AesCtr
{
public:
    AesCtr();
    ~AesCtr();

    /// 32-byte key, 16-byte initial counter block. Safe to call again to restart a connection.
    void init(const QByteArray &key32, const QByteArray &iv16);
    bool isReady() const;

    /// XORs `size` bytes in place with the keystream, advancing it.
    void apply(char *data, int size);
    void apply(QByteArray &data) { apply(data.data(), data.size()); }

private:
    struct Private;
    Private *d;

    AesCtr(const AesCtr &);               // not copyable: the keystream position is unique
    AesCtr &operator=(const AesCtr &);
};

#endif // CRYPTO_H
