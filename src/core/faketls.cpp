// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "faketls.h"

#include "crypto.h"

namespace
{
    // Record types.
    const uchar KHandshake = 0x16;
    const uchar KChangeCipherSpec = 0x14;
    const uchar KApplicationData = 0x17;

    // A TLS record's length is 16 bits, and 2^14 is the limit the specification sets for the
    // plaintext of one record - the most a server may send us in one.
    const int KMaxRecord = 16384;

    // What we send in one record: TDLib's MAX_TLS_PACKET_LENGTH (TcpTransport.h).
    const int KMaxSendRecord = 2878;

    // The digest sits at offset 11 of the hello (and of the server's reply blob): 5 bytes of
    // record header, 4 of handshake header, 2 of legacy version.
    const int KDigestOffset = 11;
    const int KDigestSize = 32;

    void u8(QByteArray &b, int v) { b.append(char(v & 0xff)); }
    void u16(QByteArray &b, int v) { b.append(char((v >> 8) & 0xff)); b.append(char(v & 0xff)); }

    /// An extension: 2-byte type, 2-byte length, body.
    void ext(QByteArray &b, int type, const QByteArray &body)
    {
        u16(b, type);
        u16(b, body.size());
        b.append(body);
    }

    /// GREASE values exist so that middleboxes learn to ignore unknown codepoints; a hello
    /// without them stands out. Both bytes are equal and of the form 0x?A.
    QByteArray greaseValues(int count)
    {
        QByteArray raw = Crypto::randomBytes(count);
        QByteArray out(count * 2, '\0');
        for (int i = 0; i < count; ++i) {
            const uchar v = uchar((uchar(raw.at(i)) & 0xf0) | 0x0a);
            out[i * 2] = char(v);
            out[i * 2 + 1] = char(v);
        }
        // Two GREASE values used in the same list must differ, or the hello looks wrong.
        if (count >= 4 && out.at(0) == out.at(4)) out[4] = char(uchar(out.at(4)) ^ 0x10);
        if (count >= 4) out[5] = out[4];
        return out;
    }

    QByteArray grease(const QByteArray &values, int index)
    {
        return values.mid(index * 2, 2);
    }
}

QByteArray FakeTls::clientHello(const QByteArray &key16, const QString &domain, int unixTime,
                                QByteArray *digest)
{
    const QByteArray g = greaseValues(7);
    const QByteArray host = domain.toUtf8();

    // ---- extensions -------------------------------------------------------------------
    QByteArray exts;

    ext(exts, (uchar(g.at(0)) << 8) | uchar(g.at(1)), QByteArray());          // GREASE

    QByteArray sni;                                                           // server_name
    u16(sni, host.size() + 3);
    u8(sni, 0);
    u16(sni, host.size());
    sni.append(host);
    ext(exts, 0x0000, sni);

    ext(exts, 0x0017, QByteArray());                                          // extended_master_secret
    QByteArray reneg; u8(reneg, 0);
    ext(exts, 0xff01, reneg);                                                 // renegotiation_info

    QByteArray groups;                                                        // supported_groups
    u16(groups, 8);
    groups.append(grease(g, 1));
    u16(groups, 0x001d);   // x25519
    u16(groups, 0x0017);   // secp256r1
    u16(groups, 0x0018);   // secp384r1
    ext(exts, 0x000a, groups);

    QByteArray formats; u8(formats, 1); u8(formats, 0);
    ext(exts, 0x000b, formats);                                               // ec_point_formats
    ext(exts, 0x0023, QByteArray());                                          // session_ticket

    QByteArray alpn;                                                          // ALPN
    u16(alpn, 12);
    u8(alpn, 2); alpn.append("h2", 2);
    u8(alpn, 8); alpn.append("http/1.1", 8);
    ext(exts, 0x0010, alpn);

    QByteArray status; u8(status, 1); u16(status, 0); u16(status, 0);
    ext(exts, 0x0005, status);                                                // status_request

    QByteArray sigalgs;                                                       // signature_algorithms
    const int algs[] = { 0x0403, 0x0804, 0x0401, 0x0503, 0x0805, 0x0501, 0x0806, 0x0601 };
    u16(sigalgs, sizeof(algs) / sizeof(algs[0]) * 2);
    for (int i = 0; i < int(sizeof(algs) / sizeof(algs[0])); ++i) u16(sigalgs, algs[i]);
    ext(exts, 0x000d, sigalgs);

    ext(exts, 0x0012, QByteArray());                                          // SCT

    QByteArray keyShare;                                                      // key_share
    QByteArray shares;
    shares.append(grease(g, 2));
    u16(shares, 1);
    u8(shares, 0);
    u16(shares, 0x001d);                                                      // x25519
    u16(shares, 32);
    // 32 random bytes. No curve arithmetic is needed: the far end is not doing a key exchange,
    // and a real public key is indistinguishable from random to anyone watching.
    shares.append(Crypto::randomBytes(32));
    u16(keyShare, shares.size());
    keyShare.append(shares);
    ext(exts, 0x0033, keyShare);

    QByteArray pskModes; u8(pskModes, 1); u8(pskModes, 1);
    ext(exts, 0x002d, pskModes);                                              // psk_key_exchange_modes

    QByteArray versions;                                                      // supported_versions
    u8(versions, 6);
    versions.append(grease(g, 3));
    u16(versions, 0x0304);
    u16(versions, 0x0303);
    ext(exts, 0x002b, versions);

    QByteArray certComp; u8(certComp, 2); u16(certComp, 0x0002);              // compress_certificate
    ext(exts, 0x001b, certComp);

    ext(exts, (uchar(g.at(8)) << 8) | uchar(g.at(9)), QByteArray(1, '\0'));   // GREASE

    // ---- body -------------------------------------------------------------------------
    QByteArray body;
    u16(body, 0x0303);                                   // legacy_version
    body.append(QByteArray(KDigestSize, '\0'));          // random: zeroed, becomes the digest
    u8(body, 32);
    body.append(Crypto::randomBytes(32));                // legacy_session_id

    QByteArray suites;
    suites.append(grease(g, 4));
    const int ciphers[] = { 0x1301, 0x1302, 0x1303, 0xc02b, 0xc02f, 0xc02c, 0xc030,
                            0xcca9, 0xcca8, 0xc013, 0xc014, 0x009c, 0x009d, 0x002f, 0x0035 };
    for (int i = 0; i < int(sizeof(ciphers) / sizeof(ciphers[0])); ++i) u16(suites, ciphers[i]);
    u16(body, suites.size());
    body.append(suites);

    u8(body, 1); u8(body, 0);                            // compression: null

    // Pad the whole hello to 517 bytes, which is what a current browser emits; a distinctive
    // length would undo the point of the exercise.
    const int framed = 5 + 4 + body.size() + 2 + exts.size();
    int padding = 517 - framed - 4;
    if (padding < 0) padding = 0;
    ext(exts, 0x0015, QByteArray(padding, '\0'));

    u16(body, exts.size());
    body.append(exts);

    // ---- framing ----------------------------------------------------------------------
    QByteArray hello;
    hello.append(char(KHandshake));
    u16(hello, 0x0301);                                  // the record claims TLS 1.0, as browsers do
    u16(hello, body.size() + 4);
    u8(hello, 0x01);                                     // handshake type: client_hello
    hello.append(char(0));
    u16(hello, body.size());
    hello.append(body);

    // ---- the digest ---------------------------------------------------------------------
    QByteArray mac = Crypto::hmacSha256(key16, hello);
    // The timestamp goes in the last four bytes, little-endian, so the proxy can reject a replay.
    for (int i = 0; i < 4; ++i)
        mac[28 + i] = char(uchar(mac.at(28 + i)) ^ uchar((unixTime >> (8 * i)) & 0xff));
    for (int i = 0; i < KDigestSize; ++i) hello[KDigestOffset + i] = mac.at(i);

    if (digest) *digest = mac;
    return hello;
}

int FakeTls::serverHelloLength(const QByteArray &r)
{
    // ServerHello record, then a ChangeCipherSpec glued to the first application-data header.
    // Laid out exactly as TDLib expects it, because that is what the proxies answer.
    static const char kHandshakePrefix[] = "\x16\x03\x03";
    static const char kRestPrefix[] = "\x14\x03\x03\x00\x01\x01\x17\x03\x03";

    int pos = 0;
    if (r.size() < pos + 3) return 0;
    if (r.mid(pos, 3) != QByteArray(kHandshakePrefix, 3)) return -1;
    pos += 3;
    if (r.size() < pos + 2) return 0;
    const int helloLen = (int(uchar(r.at(pos))) << 8) | int(uchar(r.at(pos + 1)));
    pos += 2 + helloLen;
    if (r.size() < pos + 9) return 0;
    if (r.mid(pos, 9) != QByteArray(kRestPrefix, 9)) return -1;
    pos += 9;
    if (r.size() < pos + 2) return 0;
    const int dataLen = (int(uchar(r.at(pos))) << 8) | int(uchar(r.at(pos + 1)));
    pos += 2 + dataLen;
    if (r.size() < pos) return 0;
    return pos;
}

bool FakeTls::verifyServerHello(const QByteArray &key16, const QByteArray &clientDigest,
                                const QByteArray &response)
{
    if (response.size() < KDigestOffset + KDigestSize) return false;
    const QByteArray theirs = response.mid(KDigestOffset, KDigestSize);
    QByteArray zeroed = response;
    for (int i = 0; i < KDigestSize; ++i) zeroed[KDigestOffset + i] = '\0';
    const QByteArray mine = Crypto::hmacSha256(key16, clientDigest + zeroed);
    return Crypto::constantTimeEquals(mine, theirs);
}

QByteArray FakeTls::wrap(const QByteArray &payload, bool first)
{
    QByteArray out;
    if (first) out.append("\x14\x03\x03\x00\x01\x01", 6);    // ChangeCipherSpec
    int off = 0;
    while (off < payload.size()) {
        const int take = qMin(KMaxSendRecord, payload.size() - off);
        out.append(char(KApplicationData));
        u16(out, 0x0303);
        u16(out, take);
        out.append(payload.constData() + off, take);
        off += take;
    }
    return out;
}

bool FakeTls::RecordReader::feed(const QByteArray &raw, QByteArray *out)
{
    m_buf += raw;
    for (;;) {
        if (m_buf.size() < 5) return true;                       // not a whole header yet
        const uchar type = uchar(m_buf.at(0));
        const int len = (int(uchar(m_buf.at(3))) << 8) | int(uchar(m_buf.at(4)));
        if (type != KApplicationData && type != KHandshake && type != KChangeCipherSpec)
            return false;                                        // not a TLS record stream at all
        if (len > KMaxRecord + 2048) return false;               // implausible
        if (m_buf.size() < 5 + len) return true;                 // body still arriving
        if (type == KApplicationData && out) out->append(m_buf.mid(5, len));
        m_buf.remove(0, 5 + len);
    }
}
