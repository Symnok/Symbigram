// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "mtprototransport.h"

#include "crypto.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

namespace
{
    const int ConnectTimeoutMs = 25000;
    const int MaxPacket = 16 * 1024 * 1024;
    // SOCKS5 handshake phases.
    enum { SocksNone = 0, SocksMethod, SocksAuth, SocksConnect, SocksReady };

    // Transport identifiers, repeated to fill four bytes, as the obfuscation init wants them.
    const quint32 TagIntermediate = 0xeeeeeeeeu;
    const quint32 TagPaddedIntermediate = 0xddddddddu;

    // The first four bytes of the init payload must not look like one of these, or a DPI box
    // sees a known protocol instead of random noise. From core.telegram.org/mtproto/mtproto-
    // transports: the four-byte tags, the HTTP verbs, and one more reserved value.
    bool forbiddenPrefix(const uchar *b)
    {
        const quint32 first = quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
        if (b[0] == 0xef) return true;                       // the abridged tag is a single byte
        if (first == TagIntermediate || first == TagPaddedIntermediate) return true;
        if (first == 0x44414548u) return true;               // "HEAD"
        if (first == 0x54534f50u) return true;               // "POST"
        if (first == 0x20544547u) return true;               // "GET "
        if (first == 0x4954504fu) return true;               // "OPTI"
        if (first == 0x02010316u) return true;               // reserved
        // Bytes 4..8 all zero is also disallowed.
        return b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 0;
    }
}

MtprotoTransport::MtprotoTransport(QObject *parent)
    : QObject(parent), m_tagSent(false), m_open(false), m_useProxy(false), m_socks(SocksNone)
    , m_targetPort(0), m_dcId(0), m_obfuscated(false), m_padded(false)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(m_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onError(QAbstractSocket::SocketError)));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    m_connectTimer->setInterval(ConnectTimeoutMs);
    connect(m_connectTimer, SIGNAL(timeout()), this, SLOT(onConnectTimeout()));
}

void MtprotoTransport::connectToHost(const QString &host, int port, int dcId)
{
    close();
    m_inbuf.clear();
    m_tagSent = false;
    m_open = true;
    m_targetHost = host;
    m_targetPort = port;
    m_dcId = dcId;

    // A 16-byte secret is plain obfuscation; "dd" + 16 asks for padded intermediate framing.
    m_obfuscated = false;
    m_padded = false;
    if (m_secret.size() == 16) {
        m_obfuscated = true;
    } else if (m_secret.size() == 17 && uchar(m_secret.at(0)) == 0xdd) {
        m_obfuscated = true;
        m_padded = true;
    } else if (!m_secret.isEmpty()) {
        // 0xee is FakeTLS, which needs a whole fake TLS handshake and record layer. Refusing
        // loudly beats connecting and timing out with nothing to show the user.
        //
        // Note the length: a FakeTLS secret is 0xee + 16 bytes + the proxy's DOMAIN, so it runs
        // well past 17 bytes. Testing for exactly 17 would miss every real one and complain about
        // its length instead.
        const bool fakeTls = (m_secret.size() >= 17 && uchar(m_secret.at(0)) == 0xee);
        m_open = false;
        emit disconnected(fakeTls
            ? tr("this proxy needs FakeTLS (an \"ee\" secret), which is not supported yet")
            : tr("the proxy secret must be 32 or 34 hex characters"));
        return;
    }
    m_useProxy = (m_proxy.type() == QNetworkProxy::Socks5Proxy && !m_proxy.hostName().isEmpty());
    m_socks = SocksNone;
    m_connectTimer->start();
    // Connect to the proxy (then tunnel) or straight to the datacenter. The socket keeps no
    // QNetworkProxy of its own - the SOCKS5 handshake is done by hand below.
    if (m_useProxy) m_socket->connectToHost(m_proxy.hostName(), quint16(m_proxy.port()));
    else m_socket->connectToHost(host, quint16(port));
}

void MtprotoTransport::close()
{
    m_connectTimer->stop();
    if (!m_open) return;
    m_open = false;
    m_socket->blockSignals(true);
    m_socket->abort();
    m_socket->blockSignals(false);
}

bool MtprotoTransport::isConnected() const
{
    return m_open && m_socket->state() == QAbstractSocket::ConnectedState;
}

void MtprotoTransport::sendPacket(const QByteArray &payload)
{
    if (!isConnected() || (m_useProxy && m_socks != SocksReady)) return;
    QByteArray frame;
    frame.reserve(payload.size() + 24);
    if (!m_tagSent) {
        // When obfuscated the tag travelled inside the init payload, so it is not repeated
        // here - beginStream() has already marked it sent.
        frame.append("\xee\xee\xee\xee", 4);
        m_tagSent = true;
    }
    // Padded intermediate: 0-15 random bytes after the payload, counted by the length, so
    // that packet sizes stop being multiples of four and stop matching MTProto's own.
    const QByteArray padding = m_padded
                             ? Crypto::randomBytes(int(Crypto::randomUInt64() % 16))
                             : QByteArray();
    const int len = payload.size() + padding.size();
    frame.append(char(len)).append(char(len >> 8)).append(char(len >> 16)).append(char(len >> 24));
    frame.append(payload);
    frame.append(padding);
    writeStream(frame);
}

void MtprotoTransport::onConnected()
{
    if (m_useProxy) {
        // TCP is up to the proxy; negotiate the tunnel before telling anyone we are connected.
        sendSocksGreeting();
        return;
    }
    beginStream();
}

void MtprotoTransport::beginStream()
{
    // From here on the socket carries the MTProto stream. When a proxy secret is configured the
    // obfuscation init goes first, and it carries the transport tag with it.
    if (m_obfuscated) {
        sendObfuscatedInit();
        if (!m_open) return;
        m_tagSent = true;
    }
    m_connectTimer->stop();
    emit connected();
}

void MtprotoTransport::sendObfuscatedInit()
{
    // obfuscated2, per core.telegram.org/mtproto/mtproto-transports. A 64-byte random payload
    // whose middle doubles as the AES-CTR keys: bytes 8..40 key and 40..56 IV for our writes,
    // the same offsets of the REVERSED payload for the server's. Both keys are then hashed
    // together with the proxy secret, which is what ties the stream to this particular proxy.
    QByteArray init;
    for (int attempt = 0; attempt < 64; ++attempt) {
        init = Crypto::randomBytes(64);
        if (init.size() == 64 && !forbiddenPrefix(reinterpret_cast<const uchar *>(init.constData())))
            break;
        init.clear();
    }
    if (init.size() != 64) { fail(tr("could not generate the proxy handshake")); return; }

    QByteArray reversed(init);
    for (int i = 0; i < 64; ++i) reversed[i] = init.at(63 - i);

    // The 17-byte "dd"/"ee" forms carry the mode in the first byte; only the 16 after it are key
    // material.
    const QByteArray secret = m_secret.size() == 17 ? m_secret.mid(1) : m_secret;

    const QByteArray encKey = Crypto::sha256(init.mid(8, 32) + secret);
    const QByteArray encIv = init.mid(40, 16);
    const QByteArray decKey = Crypto::sha256(reversed.mid(8, 32) + secret);
    const QByteArray decIv = reversed.mid(40, 16);
    m_enc.init(encKey, encIv);
    m_dec.init(decKey, decIv);
    if (!m_enc.isReady() || !m_dec.isReady()) { fail(tr("could not set up proxy encryption")); return; }

    // Bytes 56..60: the framing tag, four identical bytes. 60..62: the datacenter, signed
    // little-endian - a media datacenter is negative, a test one is offset by 10000. 62..64 zero.
    const uchar tagByte = m_padded ? 0xdd : 0xee;
    for (int i = 56; i < 60; ++i) init[i] = char(tagByte);
    const qint16 dc = qint16(m_dcId);
    init[60] = char(quint16(dc) & 0xff);
    init[61] = char((quint16(dc) >> 8) & 0xff);
    init[62] = 0;
    init[63] = 0;

    // The whole payload is encrypted - which also advances the keystream past these 64 bytes -
    // but only the encrypted tail is sent. The first 56 bytes travel in the clear, because the
    // server needs them to derive the very keys we just derived.
    QByteArray encrypted(init);
    m_enc.apply(encrypted);

    QByteArray out = init.left(56);
    out += encrypted.mid(56, 8);
    m_socket->write(out);
}

void MtprotoTransport::writeStream(const QByteArray &data)
{
    if (!m_obfuscated) { m_socket->write(data); return; }
    QByteArray copy(data);
    m_enc.apply(copy);
    m_socket->write(copy);
}

int MtprotoTransport::unpaddedLength(const QByteArray &frame)
{
    // A padded-intermediate length counts 0-15 random bytes that the transport gives no way to
    // identify, so the MTProto message inside has to say where it ends.
    //   - auth_key_id == 0 marks an unencrypted message, which carries its own body length.
    //   - otherwise it is auth_key_id(8) + msg_key(16) + ciphertext, and the ciphertext is a
    //     whole number of AES blocks; since the padding is under 16 bytes, rounding down to a
    //     multiple of 16 recovers it exactly.
    //   - anything shorter than that is a 4-byte transport error code.
    const uchar *b = reinterpret_cast<const uchar *>(frame.constData());
    const bool unencrypted = frame.size() >= 8
        && !b[0] && !b[1] && !b[2] && !b[3] && !b[4] && !b[5] && !b[6] && !b[7];
    if (unencrypted) {
        if (frame.size() < 20) return frame.size();
        const int inner = int(quint32(b[16]) | (quint32(b[17]) << 8)
                            | (quint32(b[18]) << 16) | (quint32(b[19]) << 24));
        const int want = 20 + inner;
        return (inner >= 0 && want <= frame.size()) ? want : frame.size();
    }
    if (frame.size() < 24) return qMin(frame.size(), 4);
    const int ciphertext = frame.size() - 24;
    return 24 + ciphertext - (ciphertext % 16);
}

void MtprotoTransport::sendSocksGreeting()
{
    QByteArray g;
    g.append(char(0x05));
    if (!m_proxy.user().isEmpty()) g.append(char(2)).append(char(0x00)).append(char(0x02));   // no-auth or user/pass
    else g.append(char(1)).append(char(0x00));                                                 // no-auth only
    m_socks = SocksMethod;
    m_socket->write(g);
}

void MtprotoTransport::sendSocksAuth()
{
    QByteArray u = m_proxy.user().toUtf8();
    QByteArray p = m_proxy.password().toUtf8();
    QByteArray a;
    a.append(char(0x01));
    a.append(char(u.size() & 0xff)).append(u.left(255));
    a.append(char(p.size() & 0xff)).append(p.left(255));
    m_socks = SocksAuth;
    m_socket->write(a);
}

void MtprotoTransport::sendSocksConnect()
{
    QByteArray c;
    c.append(char(0x05)).append(char(0x01)).append(char(0x00));    // CONNECT, reserved
    QHostAddress addr(m_targetHost);
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        quint32 ip = addr.toIPv4Address();
        c.append(char(0x01)).append(char(ip >> 24)).append(char(ip >> 16)).append(char(ip >> 8)).append(char(ip));
    } else {
        QByteArray h = m_targetHost.toUtf8();
        c.append(char(0x03)).append(char(h.size() & 0xff)).append(h.left(255));
    }
    c.append(char((m_targetPort >> 8) & 0xff)).append(char(m_targetPort & 0xff));   // port, big-endian
    m_socks = SocksConnect;
    m_socket->write(c);
}

void MtprotoTransport::processSocks()
{
    while (m_open) {
        if (m_socks == SocksMethod) {
            if (m_inbuf.size() < 2) return;
            const uchar method = uchar(m_inbuf.at(1));
            m_inbuf.remove(0, 2);
            if (method == 0x02) sendSocksAuth();
            else if (method == 0x00) sendSocksConnect();
            else { fail(tr("the proxy refused our authentication method")); return; }
        } else if (m_socks == SocksAuth) {
            if (m_inbuf.size() < 2) return;
            const uchar status = uchar(m_inbuf.at(1));
            m_inbuf.remove(0, 2);
            if (status != 0) { fail(tr("proxy authentication failed - check the username and password")); return; }
            sendSocksConnect();
        } else if (m_socks == SocksConnect) {
            if (m_inbuf.size() < 5) return;
            const uchar rep = uchar(m_inbuf.at(1));
            const uchar atyp = uchar(m_inbuf.at(3));
            int need = 4 + 2;                          // header + port
            if (atyp == 0x01) need += 4;
            else if (atyp == 0x03) need += 1 + uchar(m_inbuf.at(4));
            else if (atyp == 0x04) need += 16;
            else { fail(tr("the proxy sent a malformed reply")); return; }
            if (m_inbuf.size() < need) return;
            m_inbuf.remove(0, need);
            if (rep != 0) { fail(tr("the proxy could not reach the server (%1)").arg(int(rep))); return; }
            m_socks = SocksReady;
            // Anything still buffered is the first of the stream, not SOCKS5 - and when
            // obfuscated it arrives encrypted, so it has to be decrypted before the framing
            // loop sees it. beginStream() sets the read cipher up, hence the order here.
            {
                const QByteArray trailing = m_inbuf;
                m_inbuf.clear();
                beginStream();
                if (!m_open) return;
                m_inbuf = trailing;
                if (m_obfuscated && !m_inbuf.isEmpty()) m_dec.apply(m_inbuf);
            }
            return;
        } else {
            return;
        }
    }
}

void MtprotoTransport::onReadyRead()
{
    if (m_useProxy && m_socks != SocksReady) {
        // SOCKS5 negotiation is in the clear, so these bytes are appended untouched. Whatever is
        // left over once it completes gets decrypted inside processSocks().
        m_inbuf.append(m_socket->readAll());
        processSocks();
        if (!m_open || m_socks != SocksReady) return;   // still negotiating, or it failed
    } else {
        QByteArray chunk = m_socket->readAll();
        if (m_obfuscated) m_dec.apply(chunk);
        m_inbuf.append(chunk);
    }
    while (m_inbuf.size() >= 4) {
        const uchar *b = reinterpret_cast<const uchar *>(m_inbuf.constData());
        int len = int(quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24));
        if (len < 0 || len > MaxPacket) {
            fail(QString::fromLatin1("implausible packet length %1").arg(len));
            return;
        }
        if (m_inbuf.size() < 4 + len) return;
        QByteArray packet = m_inbuf.mid(4, len);
        m_inbuf.remove(0, 4 + len);
        // Drop the proxy's random padding before anyone tries to parse the message.
        if (m_padded) packet.truncate(unpaddedLength(packet));
        if (packet.size() == 4) {
            // A 4-byte body is a transport-level error code (a negative int32), not a message.
            const uchar *e = reinterpret_cast<const uchar *>(packet.constData());
            int code = int(quint32(e[0]) | (quint32(e[1]) << 8) | (quint32(e[2]) << 16) | (quint32(e[3]) << 24));
            fail(QString::fromLatin1("transport error from server: %1").arg(code));
            return;
        }
        emit packetReceived(packet);
        if (!m_open) return;
    }
}

void MtprotoTransport::onError(QAbstractSocket::SocketError)
{
    fail(m_socket->errorString());
}

void MtprotoTransport::onDisconnected()
{
    fail(tr("connection closed"));
}

void MtprotoTransport::onConnectTimeout()
{
    fail(tr("connection timed out"));
}

void MtprotoTransport::fail(const QString &reason)
{
    if (!m_open) return;
    close();
    emit disconnected(reason);
}
