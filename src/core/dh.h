// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The arithmetic around the handshake: factoring the server's pq challenge, and checking
// the Diffie-Hellman parameters it supplies. The DH check matters more here than in most
// clients: no server is trusted, but the prime and generator arrive from the server, and a
// client that accepts them unchecked can be fed parameters that make the shared secret cheap
// to recover. Telegram's standard 2048-bit safe prime is recognised by a byte comparison;
// anything else gets Miller-Rabin, which is the case worth being slow about.
#ifndef DH_H
#define DH_H

#include "bigint.h"

#include <QString>

namespace PqFactorization
{
    /// Splits pq into p < q; throws TlException when it cannot.
    void factor(quint64 pq, quint64 &p, quint64 &q);
    quint64 mulMod(quint64 a, quint64 b, quint64 n);
    quint64 gcd(quint64 a, quint64 b);
}

namespace DhValidation
{
    /// Full parameter check; throws TlException. `note` receives a one-line summary.
    void validateParameters(int g, const BigInt &dhPrime, const BigInt &ga, QString *note = 0);
    /// Bounds check for a DH public value: 2^(2048-64) <= value <= p - 2^(2048-64).
    void validatePublicValue(const BigInt &value, const BigInt &dhPrime, const char *name);
    bool isProbablePrime(const BigInt &n);
}

#endif // DH_H
