// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "mtprototransport.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

namespace
{
    const int ConnectTimeoutMs = 25000;
    const int MaxPacket = 16 * 1024 * 1024;
    // SOCKS5 handshake phases.
    enum { SocksNone = 0, SocksMethod, SocksAuth, SocksConnect, SocksReady };
}

MtprotoTransport::MtprotoTransport(QObject *parent)
    : QObject(parent), m_tagSent(false), m_open(false), m_useProxy(false), m_socks(SocksNone), m_targetPort(0)
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

void MtprotoTransport::connectToHost(const QString &host, int port)
{
    close();
    m_inbuf.clear();
    m_tagSent = false;
    m_open = true;
    m_targetHost = host;
    m_targetPort = port;
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
    frame.reserve(payload.size() + 8);
    if (!m_tagSent) {
        frame.append("\xee\xee\xee\xee", 4);
        m_tagSent = true;
    }
    const int len = payload.size();
    frame.append(char(len)).append(char(len >> 8)).append(char(len >> 16)).append(char(len >> 24));
    frame.append(payload);
    m_socket->write(frame);
}

void MtprotoTransport::onConnected()
{
    if (m_useProxy) {
        // TCP is up to the proxy; negotiate the tunnel before telling anyone we are connected.
        sendSocksGreeting();
        return;
    }
    m_connectTimer->stop();
    emit connected();
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
            m_connectTimer->stop();
            emit connected();
            return;                                   // any trailing bytes fall through to MTProto framing
        } else {
            return;
        }
    }
}

void MtprotoTransport::onReadyRead()
{
    m_inbuf.append(m_socket->readAll());
    if (m_useProxy && m_socks != SocksReady) {
        processSocks();
        if (!m_open || m_socks != SocksReady) return;   // still negotiating, or it failed
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
        if (len == 4) {
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
