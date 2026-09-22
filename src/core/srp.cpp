// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "srp.h"
#include "bigint.h"
#include "crypto.h"
#include "dh.h"
#include "tlconstructors.h"
#include "tlobject.h"
#include "tlreader.h"

namespace
{
    const int Pbkdf2Iterations = 100000;
    const int PadSize = 256;

    /// SH(data, salt) = SHA256(salt | data | salt).
    QByteArray sh(const QByteArray &data, const QByteArray &salt)
    {
        return Crypto::sha256(salt + data + salt);
    }
}

SrpParams Srp::readPasswordParams(const QByteArray &result)
{
    TlReader r(result);
    TlObject o = TlSchema::readObject(r);
    if (o.ctor() != Tl::AccountPassword)
        throw TlException(QString::fromLatin1("unexpected account.Password 0x%1").arg(o.ctor(), 8, 16, QLatin1Char('0')));
    SrpParams p;
    p.hasPassword = o.flag("flags", 2);
    p.hint = o.str("hint");
    if (!p.hasPassword) return p;
    TlObject algo = o.obj("current_algo");
    p.supported = algo.ctor() == Tl::PasswordKdfAlgoSha256Pbkdf2;
    if (!p.supported) return p;
    p.salt1 = algo.bytes("salt1");
    p.salt2 = algo.bytes("salt2");
    p.g = algo.intOr("g");
    p.p = algo.bytes("p");
    p.srpB = o.bytes("srp_B");
    p.srpId = o.longOr("srp_id");
    return p;
}

void SrpWorker::computeProof(const SrpParams &params, const QString &password, QByteArray &aOut, QByteArray &m1Out)
{
    BigInt p = BigInt::fromBytesBE(params.p);
    BigInt gBig = BigInt::fromUInt(quint32(params.g));
    BigInt b = BigInt::fromBytesBE(params.srpB);

    // The server chooses p and g here exactly as it does for the handshake, so the same
    // validation applies - an unchecked group would let a malicious server learn the
    // password verifier.
    DhValidation::validateParameters(params.g, p, b);

    QByteArray pPad = p.toBytesBE(PadSize);
    QByteArray gPad = gBig.toBytesBE(PadSize);
    QByteArray bPad = b.toBytesBE(PadSize);

    // x = PH2(password, salt1, salt2)
    QByteArray ph1 = sh(sh(password.toUtf8(), params.salt1), params.salt2);
    QByteArray pbkdf = Crypto::pbkdf2Sha512(ph1, params.salt1, Pbkdf2Iterations, 64);
    QByteArray x = sh(pbkdf, params.salt2);
    BigInt xBig = BigInt::fromBytesBE(x);

    // k = H(p | g), v = g^x mod p, k*v mod p
    BigInt k = BigInt::fromBytesBE(Crypto::sha256(pPad + gPad));
    BigInt v = BigInt::modPow(gBig, xBig, p);
    BigInt kv = BigInt::mod(BigInt::mul(k, v), p);

    // A = g^a mod p, retrying if u would be zero.
    BigInt a, aValue, u;
    QByteArray aPad;
    int attempts = 0;
    do {
        if (++attempts > 100) throw TlException(QLatin1String("SRP: could not find a usable secret"));
        a = BigInt::fromBytesBE(Crypto::randomBytes(256));
        aValue = BigInt::modPow(gBig, a, p);
        aPad = aValue.toBytesBE(PadSize);
        u = BigInt::fromBytesBE(Crypto::sha256(aPad + bPad));
    } while (u.isZero());

    // t = (B - k*v) mod p, kept non-negative without a signed BigInt.
    BigInt t;
    if (BigInt::compare(b, kv) >= 0) {
        t = BigInt::sub(b, kv);
    } else {
        BigInt reduced = BigInt::mod(BigInt::sub(kv, b), p);
        t = reduced.isZero() ? BigInt() : BigInt::sub(p, reduced);
    }

    // S = t^(a + u*x) mod p
    BigInt exponent = BigInt::add(a, BigInt::mul(u, xBig));
    BigInt s = BigInt::modPow(t, exponent, p);
    QByteArray kA = Crypto::sha256(s.toBytesBE(PadSize));

    QByteArray hp = Crypto::sha256(pPad);
    QByteArray hg = Crypto::sha256(gPad);
    QByteArray xored(hp.size(), '\0');
    for (int i = 0; i < hp.size(); ++i) xored[i] = char(uchar(hp.at(i)) ^ uchar(hg.at(i)));

    m1Out = Crypto::sha256(xored + Crypto::sha256(params.salt1) + Crypto::sha256(params.salt2) + aPad + bPad + kA);
    aOut = aPad;
}

void SrpWorker::run()
{
    try {
        computeProof(params, password, a, m1);
    } catch (const TlException &e) {
        error = e.message();
    }
}
