// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Minimal unsigned big integer - only what MTProto needs: ModPow for Diffie-Hellman and
// SRP, x^65537 mod n for the RSA step, and the comparisons around them. Values are
// unsigned and immutable. Limbs are quint32, little-endian; callers deal in big-endian byte
// strings, because that is how MTProto carries integers. Ported from LumigramPlus's BigInt,
// which was differential-tested against a reference implementation.
#ifndef BIGINT_H
#define BIGINT_H

#include <QByteArray>
#include <QString>
#include <QVector>

class BigInt
{
public:
    BigInt() {}
    static BigInt fromUInt(quint32 v);
    static BigInt fromUInt64(quint64 v);
    /// Parses a big-endian byte string, as MTProto transmits integers.
    static BigInt fromBytesBE(const QByteArray &b);
    static BigInt one() { return fromUInt(1); }

    /// Big-endian bytes. When size is positive the result is left-padded or trimmed to
    /// exactly that length (DH values are always sent as 256 bytes); -1 means minimal.
    QByteArray toBytesBE(int size = -1) const;

    bool isZero() const { return m_d.isEmpty(); }
    int bitLength() const;
    bool testBit(int i) const;

    static int compare(const BigInt &a, const BigInt &b);
    static BigInt add(const BigInt &a, const BigInt &b);
    /// a - b, requiring a >= b (unsigned type: negatives cannot be represented).
    static BigInt sub(const BigInt &a, const BigInt &b);
    static BigInt mul(const BigInt &a, const BigInt &b);
    static BigInt mod(const BigInt &a, const BigInt &m);
    /// Knuth algorithm D.
    static void divMod(const BigInt &a, const BigInt &b, BigInt &quotient, BigInt &remainder);
    /// Left-to-right square-and-multiply. Not constant time: the exponents are ephemeral
    /// secrets on a single-user device.
    static BigInt modPow(const BigInt &b, const BigInt &e, const BigInt &m);
    /// (n - 1) / 2 for odd n, or n / 2 for even n: a right shift by one bit.
    BigInt shiftRightOne() const;

    QString toString() const;

private:
    explicit BigInt(const QVector<quint32> &limbs) : m_d(limbs) { trim(); }
    void trim();
    static QVector<quint32> shiftLeft(const QVector<quint32> &a, int shift, int size);
    static QVector<quint32> shiftRight(const QVector<quint32> &a, int shift, int len);
    QVector<quint32> m_d;   // little-endian limbs, no leading zeros
};

#endif // BIGINT_H
