// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// FakeTLS, the "ee" MTProto proxy mode: the connection is dressed up as an ordinary HTTPS session
// so that a censor who allows only recognisable TLS lets it through. None of the TLS is real -
// there is no certificate, no key exchange and no AEAD. The proxy checks one HMAC to see that we
// know the secret, we check one HMAC to see that it does, and after that the obfuscated2 stream
// (see mtprototransport.cpp) is carried inside TLS application-data records.
//
// Byte layout follows TDLib's td/mtproto/TlsInit.cpp, which is what the proxies are written
// against:
//   - the ClientHello's 32-byte random sits at offset 11 and starts out zeroed;
//   - digest = HMAC-SHA256(secret, whole hello with that field zeroed), written back at 11;
//   - digest[28..32] ^= the unix time, as a little-endian int32 - which is why the phone's clock
//     matters (see Transport::setTimeOffset);
//   - the server answers with a ServerHello record, a ChangeCipherSpec and an application-data
//     record, and its own 32 bytes at offset 11 of that whole blob authenticate it. That
//     application-data record is FILLER (random bytes from a real proxy) and is discarded;
//   - the client's first record afterwards is preceded by a ChangeCipherSpec of its own
//     (TcpTransport.cpp, do_write_tls), and its records are cut at 2878 bytes.
#ifndef FAKETLS_H
#define FAKETLS_H

#include <QByteArray>
#include <QString>

namespace FakeTls
{
    /// A TLS 1.3 ClientHello carrying the proxy digest. `digest` receives the 32 bytes written at
    /// offset 11; they are needed to check the server's reply. `unixTime` should be the real
    /// time, corrected for the phone's clock error.
    QByteArray clientHello(const QByteArray &key16, const QString &domain, int unixTime,
                           QByteArray *digest);

    /// The length of the server's complete handshake blob - the ServerHello record, the
    /// ChangeCipherSpec and the first application-data record, exactly as received. 0 while it
    /// is still arriving, -1 when it is not a TLS reply at all.
    int serverHelloLength(const QByteArray &received);

    /// True when the server proved it knows the secret: HMAC-SHA256(secret, clientDigest +
    /// response-with-its-own-32-bytes-at-11-zeroed) must equal those 32 bytes.
    bool verifyServerHello(const QByteArray &key16, const QByteArray &clientDigest,
                           const QByteArray &response);

    /// Wraps a payload in application-data records of at most 2878 bytes, as TDLib cuts them.
    /// `first` puts the client's one-off ChangeCipherSpec in front.
    QByteArray wrap(const QByteArray &payload, bool first);

    /// Pulls application-data payloads out of a record stream. Records split across TCP reads,
    /// so the leftover is kept between calls.
    class RecordReader
    {
    public:
        RecordReader() {}
        /// Appends any application data found to `out`. False if the stream is malformed.
        bool feed(const QByteArray &raw, QByteArray *out);
        void reset() { m_buf.clear(); }

    private:
        QByteArray m_buf;
    };
}

#endif // FAKETLS_H
