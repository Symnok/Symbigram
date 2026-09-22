// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "mtprototransport.h"

#include <QTcpSocket>
#include <QTimer>

namespace
{
    const int ConnectTimeoutMs = 25000;
    const int MaxPacket = 16 * 1024 * 1024;
}

MtprotoTransport::MtprotoTransport(QObject *parent)
    : QObject(parent), m_tagSent(false), m_open(false)
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
    m_socket->setProxy(m_proxy);
    m_connectTimer->start();
    m_socket->connectToHost(host, quint16(port));
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
    if (!isConnected()) return;
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
    m_connectTimer->stop();
    emit connected();
}

void MtprotoTransport::onReadyRead()
{
    m_inbuf.append(m_socket->readAll());
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
