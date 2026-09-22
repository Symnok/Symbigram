// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Two-step verification, as passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow.
// SRP means the password never crosses the wire; the client proves knowledge of it by
// deriving a value the server can check:
//     x  = PH2(password, salt1, salt2)     v = g^x mod p     A = g^a mod p
//     u  = H(A | B)                        S = (B - k*v)^(a + u*x) mod p
//     M1 = H(H(p) xor H(g) | H(salt1) | H(salt2) | A | B | H(S))
// Every large value is padded to 256 bytes before hashing. 100,000 PBKDF2 rounds and a
// 2048-bit exponentiation take seconds on the phone, so the proof is computed in a thread.
#ifndef SRP_H
#define SRP_H

#include <QByteArray>
#include <QString>
#include <QThread>

/// The server's account.password parameters this attempt is checked against.
struct SrpParams
{
    SrpParams() : g(0), srpId(0), hasPassword(false), supported(false) {}
    QByteArray salt1, salt2, p, srpB;
    int g;
    qint64 srpId;
    bool hasPassword;
    bool supported;
    QString hint;
};

class SrpWorker : public QThread
{
    Q_OBJECT
public:
    explicit SrpWorker(QObject *parent = 0) : QThread(parent) {}
    SrpParams params;       // input
    QString password;       // input
    QByteArray a, m1;       // outputs: A (256 bytes) and M1 (32 bytes)
    QString error;

    /// The computation itself, also callable synchronously (the harness self test).
    static void computeProof(const SrpParams &params, const QString &password, QByteArray &aOut, QByteArray &m1Out);

protected:
    void run();
};

namespace Srp
{
    /// Reads account.password (the raw TL bytes of the result).
    SrpParams readPasswordParams(const QByteArray &result);
}

#endif // SRP_H
