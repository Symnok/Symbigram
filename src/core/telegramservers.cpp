// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "telegramservers.h"
#include "crypto.h"
#include "tlreader.h"
#include "tlwriter.h"

#include <QStringList>

namespace
{
    const char *const PublicKeyPems[] = {
        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCgKCAQEAwVACPi9w23mF3tBkdZz+zwrzKOaaQdr01vAbU4E1pvkfj4sqDsm6\n"
        "lyDONS789sVoD/xCS9Y0hkkC3gtL1tSfTlgCMOOul9lcixlEKzwKENj1Yz/s7daS\n"
        "an9tqw3bfUV/nqgbhGX81v/+7RFAEd+RwFnK7a+XYl9sluzHRyVVaTTveB2GazTw\n"
        "Efzk2DWgkBluml8OREmvfraX3bkHZJTKX4EQSjBbbdJ2ZXIsRrYOXfaA+xayEGB+\n"
        "8hdlLmAjbCVfaigxX0CDqWeR1yFL9kwd9P0NsZRPsmoqVwMbMu7mStFai6aIhc3n\n"
        "Slv8kg9qv1m6XHVQY3PnEw+QQtqSIXklHwIDAQAB\n"
        "-----END RSA PUBLIC KEY-----",

        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCgKCAQEAxq7aeLAqJR20tkQQMfRn+ocfrtMlJsQ2Uksfs7Xcoo77jAid0bRt\n"
        "ksiVmT2HEIJUlRxfABoPBV8wY9zRTUMaMA654pUX41mhyVN+XoerGxFvrs9dF1Ru\n"
        "vCHbI02dM2ppPvyytvvMoefRoL5BTcpAihFgm5xCaakgsJ/tH5oVl74CdhQw8J5L\n"
        "xI/K++KJBUyZ26Uba1632cOiq05JBUW0Z2vWIOk4BLysk7+U9z+SxynKiZR3/xdi\n"
        "XvFKk01R3BHV+GUKM2RYazpS/P8v7eyKhAbKxOdRcFpHLlVwfjyM1VlDQrEZxsMp\n"
        "NTLYXb6Sce1Uov0YtNx5wEowlREH1WOTlwIDAQAB\n"
        "-----END RSA PUBLIC KEY-----",

        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCgKCAQEAsQZnSWVZNfClk29RcDTJQ76n8zZaiTGuUsi8sUhW8AS4PSbPKDm+\n"
        "DyJgdHDWdIF3HBzl7DHeFrILuqTs0vfS7Pa2NW8nUBwiaYQmPtwEa4n7bTmBVGsB\n"
        "1700/tz8wQWOLUlL2nMv+BPlDhxq4kmJCyJfgrIrHlX8sGPcPA4Y6Rwo0MSqYn3s\n"
        "g1Pu5gOKlaT9HKmE6wn5Sut6IiBjWozrRQ6n5h2RXNtO7O2qCDqjgB2vBxhV7B+z\n"
        "hRbLbCmW0tYMDsvPpX5M8fsO05svN+lKtCAuz1leFns8piZpptpSCFn7bWxiA9/f\n"
        "x5x17D7pfah3Sy2pA+NDXyzSlGcKdaUmwQIDAQAB\n"
        "-----END RSA PUBLIC KEY-----",

        "-----BEGIN RSA PUBLIC KEY-----\n"
        "MIIBCgKCAQEAwqjFW0pi4reKGbkc9pK83Eunwj/k0G8ZTioMMPbZmW99GivMibwa\n"
        "xDM9RDWabEMyUtGoQC2ZcDeLWRK3W8jMP6dnEKAlvLkDLfC4fXYHzFO5KHEqF06i\n"
        "qAqBdmI1iBGdQv/OQCBcbXIWCGDY2AsiqLhlGQfPOI7/vvKc188rTriocgUtoTUc\n"
        "/n/sIUzkgwTqRyvWYynWARWzQg0I9olLBBC2q5RQJJlnYXZwyTL3y9tdb7zOHkks\n"
        "WV9IMQmZmyZh/N7sMbGWQpt4NMchGpPGeJ2e5gHBjDnlIf2p1yZOYeUYrdbwcS0t\n"
        "UiggS4UeE8TzIuXFQxw7fzEIlmhIaq3FnwIDAQAB\n"
        "-----END RSA PUBLIC KEY-----",
    };

    int readDerHeader(const QByteArray &der, int &pos, uchar expectedTag)
    {
        if (pos >= der.size() || uchar(der.at(pos)) != expectedTag)
            throw TlException(QString::fromLatin1("DER: expected tag 0x%1 at %2").arg(expectedTag, 2, 16, QLatin1Char('0')).arg(pos));
        ++pos;
        int len = uchar(der.at(pos++));
        if (len & 0x80) {
            int count = len & 0x7F;
            if (count == 0 || count > 4) throw TlException(QLatin1String("DER: bad length form"));
            len = 0;
            for (int i = 0; i < count; ++i) len = (len << 8) | uchar(der.at(pos++));
        }
        if (pos + len > der.size()) throw TlException(QLatin1String("DER: length runs past end"));
        return len;
    }

    QByteArray readDerInteger(const QByteArray &der, int &pos)
    {
        int len = readDerHeader(der, pos, 0x02);
        int start = pos;
        pos += len;
        // DER integers are signed: a positive value with its top bit set carries a
        // leading zero byte that is not part of the number.
        if (len > 1 && der.at(start) == 0) { ++start; --len; }
        return der.mid(start, len);
    }
}

RsaKey::RsaKey(const QByteArray &modulusBE, const QByteArray &exponentBE)
    : m_modulus(BigInt::fromBytesBE(modulusBE)), m_exponent(BigInt::fromBytesBE(exponentBE))
{
    TlWriter w(320);
    w.writeBytes(modulusBE);
    w.writeBytes(exponentBE);
    QByteArray hash = Crypto::sha1(w.toByteArray());
    quint64 fp = 0;
    for (int i = 0; i < 8; ++i) fp |= quint64(uchar(hash.at(hash.size() - 8 + i))) << (8 * i);
    m_fingerprint = qint64(fp);
}

RsaKey RsaKey::fromPem(const QString &pem)
{
    QString body;
    QStringList lines = pem.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("-----"))) continue;
        body += line;
    }
    QByteArray der = QByteArray::fromBase64(body.toLatin1());
    int pos = 0;
    readDerHeader(der, pos, 0x30);          // SEQUENCE
    QByteArray n = readDerInteger(der, pos);
    QByteArray e = readDerInteger(der, pos);
    return RsaKey(n, e);
}

QByteArray RsaKey::encrypt(const QByteArray &data) const
{
    BigInt m = BigInt::fromBytesBE(data);
    if (BigInt::compare(m, m_modulus) >= 0) throw TlException(QLatin1String("RSA block is not smaller than the modulus"));
    return BigInt::modPow(m, m_exponent, m_modulus).toBytesBE(256);
}

QList<RsaKey> TelegramServers::publicKeys()
{
    static QList<RsaKey> keys;
    if (keys.isEmpty())
        for (unsigned i = 0; i < sizeof(PublicKeyPems) / sizeof(PublicKeyPems[0]); ++i)
            keys.append(RsaKey::fromPem(QLatin1String(PublicKeyPems[i])));
    return keys;
}

RsaKey TelegramServers::findByFingerprint(const QList<qint64> &offered)
{
    QList<RsaKey> keys = publicKeys();
    for (int i = 0; i < offered.size(); ++i)
        for (int k = 0; k < keys.size(); ++k)
            if (keys.at(k).fingerprint() == offered.at(i)) return keys.at(k);
    return RsaKey();
}

QString TelegramServers::hostFor(int dcId)
{
    switch (dcId) {
    case 1: return QLatin1String("149.154.175.50");
    case 2: return QLatin1String("149.154.167.51");
    case 3: return QLatin1String("149.154.175.100");
    case 4: return QLatin1String("149.154.167.91");
    case 5: return QLatin1String("149.154.171.5");
    default: return QLatin1String("149.154.167.51");
    }
}
