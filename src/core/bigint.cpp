// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "bigint.h"
#include "tlreader.h"

void BigInt::trim()
{
    int n = m_d.size();
    while (n > 0 && m_d.at(n - 1) == 0) --n;
    if (n != m_d.size()) m_d.resize(n);
}

BigInt BigInt::fromUInt(quint32 v)
{
    QVector<quint32> d;
    if (v) d.append(v);
    return BigInt(d);
}

BigInt BigInt::fromUInt64(quint64 v)
{
    QVector<quint32> d;
    d.append(quint32(v));
    d.append(quint32(v >> 32));
    return BigInt(d);
}

BigInt BigInt::fromBytesBE(const QByteArray &b)
{
    if (b.isEmpty()) return BigInt();
    const int limbs = (b.size() + 3) / 4;
    QVector<quint32> d(limbs, 0);
    const uchar *p = reinterpret_cast<const uchar *>(b.constData());
    for (int i = 0; i < b.size(); ++i) {
        int be = b.size() - 1 - i;           // byte index counted from the low end
        d[be >> 2] |= quint32(p[i]) << (8 * (be & 3));
    }
    return BigInt(d);
}

QByteArray BigInt::toBytesBE(int size) const
{
    const int len = m_d.size() * 4;
    if (len == 0) return QByteArray(size < 0 ? 1 : size, '\0');

    QByteArray full(len, '\0');
    for (int i = 0; i < len; ++i)
        full[len - 1 - i] = char(m_d.at(i >> 2) >> (8 * (i & 3)));

    int first = 0;
    while (first < len - 1 && full.at(first) == 0) ++first;
    const int sig = len - first;

    if (size < 0) return full.mid(first, sig);

    QByteArray p(size, '\0');
    const int copy = qMin(sig, size);
    memcpy(p.data() + size - copy, full.constData() + first + (sig - copy), copy);
    return p;
}

int BigInt::bitLength() const
{
    if (m_d.isEmpty()) return 0;
    quint32 hi = m_d.last();
    int bits = 0;
    while (hi) { ++bits; hi >>= 1; }
    return (m_d.size() - 1) * 32 + bits;
}

bool BigInt::testBit(int i) const
{
    int limb = i >> 5;
    return limb < m_d.size() && ((m_d.at(limb) >> (i & 31)) & 1) != 0;
}

int BigInt::compare(const BigInt &a, const BigInt &b)
{
    if (a.m_d.size() != b.m_d.size()) return a.m_d.size() < b.m_d.size() ? -1 : 1;
    for (int i = a.m_d.size() - 1; i >= 0; --i)
        if (a.m_d.at(i) != b.m_d.at(i)) return a.m_d.at(i) < b.m_d.at(i) ? -1 : 1;
    return 0;
}

BigInt BigInt::add(const BigInt &a, const BigInt &b)
{
    const int n = qMax(a.m_d.size(), b.m_d.size());
    QVector<quint32> r(n + 1, 0);
    quint64 carry = 0;
    for (int i = 0; i < n; ++i) {
        quint64 s = carry;
        if (i < a.m_d.size()) s += a.m_d.at(i);
        if (i < b.m_d.size()) s += b.m_d.at(i);
        r[i] = quint32(s);
        carry = s >> 32;
    }
    r[n] = quint32(carry);
    return BigInt(r);
}

BigInt BigInt::sub(const BigInt &a, const BigInt &b)
{
    if (compare(a, b) < 0) throw TlException(QLatin1String("BigInt::sub would go negative"));
    QVector<quint32> r(a.m_d.size(), 0);
    qint64 borrow = 0;
    for (int i = 0; i < a.m_d.size(); ++i) {
        qint64 s = qint64(a.m_d.at(i)) - borrow - (i < b.m_d.size() ? qint64(b.m_d.at(i)) : 0);
        if (s < 0) { s += qint64(1) << 32; borrow = 1; } else borrow = 0;
        r[i] = quint32(s);
    }
    return BigInt(r);
}

BigInt BigInt::mul(const BigInt &a, const BigInt &b)
{
    if (a.isZero() || b.isZero()) return BigInt();
    const int an = a.m_d.size(), bn = b.m_d.size();
    QVector<quint32> r(an + bn, 0);
    quint32 *rp = r.data();
    const quint32 *ap = a.m_d.constData();
    const quint32 *bp = b.m_d.constData();
    for (int i = 0; i < an; ++i) {
        quint64 carry = 0;
        const quint64 ai = ap[i];
        if (ai == 0) continue;
        for (int j = 0; j < bn; ++j) {
            quint64 t = ai * bp[j] + rp[i + j] + carry;
            rp[i + j] = quint32(t);
            carry = t >> 32;
        }
        int k = i + bn;
        while (carry != 0) { quint64 t = quint64(rp[k]) + carry; rp[k] = quint32(t); carry = t >> 32; ++k; }
    }
    return BigInt(r);
}

BigInt BigInt::mod(const BigInt &a, const BigInt &m)
{
    BigInt q, rem;
    divMod(a, m, q, rem);
    return rem;
}

QVector<quint32> BigInt::shiftLeft(const QVector<quint32> &a, int shift, int size)
{
    QVector<quint32> r(size, 0);
    if (shift == 0) {
        for (int i = 0; i < qMin(a.size(), size); ++i) r[i] = a.at(i);
        return r;
    }
    quint32 carry = 0;
    const int lim = qMin(a.size(), size);
    for (int i = 0; i < lim; ++i) {
        r[i] = (a.at(i) << shift) | carry;
        carry = a.at(i) >> (32 - shift);
    }
    if (a.size() < size) r[a.size()] = carry;
    return r;
}

QVector<quint32> BigInt::shiftRight(const QVector<quint32> &a, int shift, int len)
{
    QVector<quint32> r(len, 0);
    if (shift == 0) {
        for (int i = 0; i < qMin(a.size(), len); ++i) r[i] = a.at(i);
        return r;
    }
    for (int i = 0; i < len; ++i) {
        quint32 lo = a.at(i) >> shift;
        quint32 hi = (i + 1 < a.size()) ? (a.at(i + 1) << (32 - shift)) : 0;
        r[i] = lo | hi;
    }
    return r;
}

void BigInt::divMod(const BigInt &a, const BigInt &b, BigInt &quotient, BigInt &remainder)
{
    if (b.isZero()) throw TlException(QLatin1String("BigInt: division by zero"));
    if (compare(a, b) < 0) { quotient = BigInt(); remainder = a; return; }

    if (b.m_d.size() == 1) {
        const quint64 d = b.m_d.at(0);
        quint64 r0 = 0;
        QVector<quint32> q1(a.m_d.size(), 0);
        for (int i = a.m_d.size() - 1; i >= 0; --i) {
            quint64 cur = (r0 << 32) | a.m_d.at(i);
            q1[i] = quint32(cur / d);
            r0 = cur % d;
        }
        quotient = BigInt(q1);
        remainder = fromUInt(quint32(r0));
        return;
    }

    // Normalise so the divisor's top limb has its high bit set - algorithm D requires
    // this for the qhat estimate to be within one of the truth.
    int shift = 0;
    quint32 hi = b.m_d.last();
    while ((hi & 0x80000000u) == 0) { hi <<= 1; ++shift; }

    QVector<quint32> u = shiftLeft(a.m_d, shift, a.m_d.size() + 1);
    QVector<quint32> v = shiftLeft(b.m_d, shift, b.m_d.size());

    const int n = v.size(), m = u.size() - n - 1;
    QVector<quint32> q(m + 1, 0);
    quint32 *up = u.data();
    const quint32 *vp = v.constData();
    const quint64 vHigh = vp[n - 1], vNext = vp[n - 2];

    for (int j = m; j >= 0; --j) {
        quint64 num = (quint64(up[j + n]) << 32) | up[j + n - 1];
        quint64 qhat = num / vHigh, rhat = num % vHigh;
        while (qhat > 0xFFFFFFFFull || qhat * vNext > ((rhat << 32) | up[j + n - 2])) {
            --qhat;
            rhat += vHigh;
            if (rhat > 0xFFFFFFFFull) break;
        }

        qint64 borrow = 0;
        quint64 carry = 0;
        for (int i = 0; i < n; ++i) {
            quint64 p = qhat * vp[i] + carry;
            carry = p >> 32;
            qint64 t = qint64(up[i + j]) - qint64(quint32(p)) - borrow;
            if (t < 0) { t += qint64(1) << 32; borrow = 1; } else borrow = 0;
            up[i + j] = quint32(t);
        }
        qint64 tn = qint64(up[j + n]) - qint64(carry) - borrow;
        if (tn < 0) {
            // qhat was one too large: give the digit back and add the divisor in.
            tn += qint64(1) << 32;
            up[j + n] = quint32(tn);
            --qhat;
            quint64 c2 = 0;
            for (int i = 0; i < n; ++i) {
                quint64 s = quint64(up[i + j]) + vp[i] + c2;
                up[i + j] = quint32(s);
                c2 = s >> 32;
            }
            up[j + n] = quint32(up[j + n] + c2);
        } else {
            up[j + n] = quint32(tn);
        }
        q[j] = quint32(qhat);
    }

    quotient = BigInt(q);
    remainder = BigInt(shiftRight(u, shift, n));
}

BigInt BigInt::modPow(const BigInt &b, const BigInt &e, const BigInt &m)
{
    if (m.isZero()) throw TlException(QLatin1String("BigInt: modPow by zero"));
    if (e.isZero()) return one();
    BigInt result = one(), bas = mod(b, m);
    for (int i = e.bitLength() - 1; i >= 0; --i) {
        result = mod(mul(result, result), m);
        if (e.testBit(i)) result = mod(mul(result, bas), m);
    }
    return result;
}

BigInt BigInt::shiftRightOne() const
{
    return BigInt(shiftRight(m_d, 1, m_d.size()));
}

QString BigInt::toString() const
{
    if (isZero()) return QLatin1String("0");
    return QLatin1String("0x") + QString::fromLatin1(toBytesBE().toHex());
}
