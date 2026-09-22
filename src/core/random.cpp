// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Secure random bytes from the platform: the kernel random pool on Symbian
// (Math::Random, the same source the Symbian crypto library draws on), CryptGenRandom on
// Windows, /dev/urandom elsewhere. Nonces and the DH secret depend on this, so qrand() is
// never used.
#include "crypto.h"
#include "tlreader.h"

#if defined(Q_OS_SYMBIAN)
#include <e32math.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincrypt.h>
#else
#include <QFile>
#endif

QByteArray Crypto::randomBytes(int count)
{
    QByteArray out(count, '\0');
#if defined(Q_OS_SYMBIAN)
    for (int i = 0; i < count; i += 4) {
        TUint32 r = Math::Random();
        for (int j = 0; j < 4 && i + j < count; ++j) out[i + j] = char(r >> (8 * j));
    }
#elif defined(Q_OS_WIN)
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContextW(&prov, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        throw TlException(QLatin1String("CryptAcquireContext failed"));
    BOOL ok = CryptGenRandom(prov, DWORD(count), reinterpret_cast<BYTE *>(out.data()));
    CryptReleaseContext(prov, 0);
    if (!ok) throw TlException(QLatin1String("CryptGenRandom failed"));
#else
    QFile f(QLatin1String("/dev/urandom"));
    if (!f.open(QIODevice::ReadOnly) || f.read(out.data(), count) != count)
        throw TlException(QLatin1String("/dev/urandom unavailable"));
#endif
    return out;
}

quint64 Crypto::randomUInt64()
{
    QByteArray b = randomBytes(8);
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v |= quint64(uchar(b.at(i))) << (8 * i);
    return v;
}
