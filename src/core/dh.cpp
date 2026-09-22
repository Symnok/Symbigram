// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "dh.h"
#include "crypto.h"
#include "tlreader.h"

#include <QList>

// -- pq factorisation (Brent's variant of Pollard's rho) ------------------------------------------------

namespace
{
    inline quint64 diff(quint64 a, quint64 b) { return a > b ? a - b : b - a; }

    inline quint64 step(quint64 y, quint64 c, quint64 n)
    {
        quint64 v = PqFactorization::mulMod(y, y, n) + c;
        return v >= n ? v - n : v;
    }

    quint64 findFactor(quint64 n)
    {
        if (n % 2 == 0) return 2;
        for (quint64 c = 1; c < 20; ++c) {
            quint64 x = 2, y = 2, d = 1;
            quint64 r = 1, qAcc = 1;
            quint64 ys = 0;
            while (d == 1) {
                x = y;
                for (quint64 i = 0; i < r; ++i) y = step(y, c, n);
                quint64 k = 0;
                while (k < r && d == 1) {
                    ys = y;
                    quint64 limit = qMin(quint64(128), r - k);
                    for (quint64 i = 0; i < limit; ++i) {
                        y = step(y, c, n);
                        qAcc = PqFactorization::mulMod(qAcc, diff(x, y), n);
                    }
                    d = PqFactorization::gcd(qAcc, n);
                    k += limit;
                }
                r *= 2;
                if (r > (Q_UINT64_C(1) << 40)) break;
            }
            if (d == n) {
                d = 1;
                quint64 y2 = ys;
                while (d == 1) {
                    y2 = step(y2, c, n);
                    d = PqFactorization::gcd(diff(x, y2), n);
                }
            }
            if (d != 1 && d != n) return d;
        }
        throw TlException(QString::fromLatin1("could not factor pq = %1").arg(n));
    }
}

quint64 PqFactorization::mulMod(quint64 a, quint64 b, quint64 n)
{
    // (a * b) mod n without overflowing 64 bits, by shift-and-add: neither the phone's
    // compiler nor Qt offers a 128-bit multiply.
    quint64 result = 0;
    a %= n;
    while (b > 0) {
        if (b & 1) {
            result += a;
            if (result >= n || result < a) result -= n;
        }
        quint64 t = a;
        a += a;
        if (a >= n || a < t) a -= n;
        b >>= 1;
    }
    return result;
}

quint64 PqFactorization::gcd(quint64 a, quint64 b)
{
    while (b != 0) { quint64 t = a % b; a = b; b = t; }
    return a;
}

void PqFactorization::factor(quint64 pq, quint64 &p, quint64 &q)
{
    quint64 f = findFactor(pq);
    quint64 other = pq / f;
    p = qMin(f, other);
    q = qMax(f, other);
    if (p * q != pq) throw TlException(QString::fromLatin1("failed to factor pq = %1").arg(pq));
}

// -- DH validation ---------------------------------------------------------------------------------------

namespace
{
    const int MillerRabinRounds = 12;

    // The 2048-bit safe prime Telegram serves to everyone (TDLib compares the same
    // built-in prime before doing any work).
    const char *const BuiltInGoodPrimeHex =
        "c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f"
        "48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c37"
        "20fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f64"
        "2477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4"
        "a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754"
        "fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4"
        "e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f"
        "0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b";

    QList<QByteArray> &validatedPrimes() { static QList<QByteArray> list; return list; }

    BigInt powerOfTwo(int exponent)
    {
        QByteArray bytes(exponent / 8 + 1, '\0');
        bytes[0] = char(1 << (exponent % 8));
        return BigInt::fromBytesBE(bytes);
    }

    BigInt randomBase(int byteLen, const BigInt &upperExclusive)
    {
        while (true) {
            BigInt a = BigInt::fromBytesBE(Crypto::randomBytes(byteLen));
            if (BigInt::compare(a, BigInt::fromUInt(2)) >= 0 && BigInt::compare(a, upperExclusive) < 0) return a;
        }
    }
}

void DhValidation::validatePublicValue(const BigInt &value, const BigInt &dhPrime, const char *name)
{
    QString n = QLatin1String(name);
    if (BigInt::compare(value, BigInt::one()) <= 0) throw TlException(n + QLatin1String(" is too small"));
    BigInt pMinus1 = BigInt::sub(dhPrime, BigInt::one());
    if (BigInt::compare(value, pMinus1) >= 0) throw TlException(n + QLatin1String(" is too large"));
    BigInt lower = powerOfTwo(2048 - 64);
    BigInt upper = BigInt::sub(dhPrime, lower);
    if (BigInt::compare(value, lower) < 0) throw TlException(n + QLatin1String(" is within 2^1984 of zero"));
    if (BigInt::compare(value, upper) > 0) throw TlException(n + QLatin1String(" is within 2^1984 of the prime"));
}

bool DhValidation::isProbablePrime(const BigInt &n)
{
    if (BigInt::compare(n, BigInt::fromUInt(2)) < 0) return false;
    const quint32 smallPrimes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    for (unsigned i = 0; i < sizeof(smallPrimes) / sizeof(smallPrimes[0]); ++i) {
        BigInt p = BigInt::fromUInt(smallPrimes[i]);
        if (BigInt::compare(n, p) == 0) return true;
        if (BigInt::mod(n, p).isZero()) return false;
    }
    // n - 1 = d * 2^s with d odd
    BigInt nMinus1 = BigInt::sub(n, BigInt::one());
    BigInt d = nMinus1;
    int s = 0;
    while (!d.testBit(0)) { d = d.shiftRightOne(); ++s; }
    const int byteLen = (n.bitLength() + 7) / 8;

    for (int round = 0; round < MillerRabinRounds; ++round) {
        BigInt a = randomBase(byteLen, nMinus1);
        BigInt x = BigInt::modPow(a, d, n);
        if (BigInt::compare(x, BigInt::one()) == 0 || BigInt::compare(x, nMinus1) == 0) continue;
        bool witnessed = false;
        for (int i = 1; i < s; ++i) {
            x = BigInt::mod(BigInt::mul(x, x), n);
            if (BigInt::compare(x, nMinus1) == 0) { witnessed = true; break; }
        }
        if (!witnessed) return false;
    }
    return true;
}

void DhValidation::validateParameters(int g, const BigInt &dhPrime, const BigInt &ga, QString *note)
{
    validatePrime(g, dhPrime, note);
    validatePublicValue(ga, dhPrime, "g_a");
}

void DhValidation::validatePrime(int g, const BigInt &dhPrime, QString *note)
{
    if (g < 2 || g > 7) throw TlException(QString::fromLatin1("DH generator out of range: %1").arg(g));
    if (dhPrime.bitLength() != 2048)
        throw TlException(QString::fromLatin1("DH prime is %1 bits, expected 2048").arg(dhPrime.bitLength()));

    QByteArray primeBytes = dhPrime.toBytesBE(256);
    static QByteArray builtIn;
    if (builtIn.isEmpty()) builtIn = QByteArray::fromHex(BuiltInGoodPrimeHex);
    if (Crypto::constantTimeEquals(builtIn, primeBytes)) {
        if (note) *note = QLatin1String("dh_prime: recognised as Telegram's standard safe prime");
        return;
    }
    QList<QByteArray> &known = validatedPrimes();
    for (int i = 0; i < known.size(); ++i)
        if (Crypto::constantTimeEquals(known.at(i), primeBytes)) {
            if (note) *note = QLatin1String("dh_prime: previously validated");
            return;
        }

    // Telegram requires a safe prime: both p and (p-1)/2 must be prime.
    if (!isProbablePrime(dhPrime)) throw TlException(QLatin1String("DH prime is not prime"));
    if (!isProbablePrime(dhPrime.shiftRightOne()))
        throw TlException(QLatin1String("DH prime is not a safe prime: (p-1)/2 is composite"));
    if (known.size() < 8) known.append(primeBytes);
    if (note) *note = QLatin1String("dh_prime: valid safe prime (validated from scratch)");
}
