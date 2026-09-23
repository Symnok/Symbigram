// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A TCP connection to one datacenter with MTProto's "intermediate" framing: a one-off
// 0xeeeeeeee tag, then every packet prefixed with its little-endian length. Chosen over the
// abridged format because the length field is a plain uint32 in both directions, with no
// size-dependent special case to get wrong.
#ifndef MTPROTOTRANSPORT_H
#define MTPROTOTRANSPORT_H

#include <QAbstractSocket>
#include <QByteArray>
#include <QNetworkProxy>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

class MtprotoTransport : public QObject
{
    Q_OBJECT
public:
    explicit MtprotoTransport(QObject *parent = 0);

    void connectToHost(const QString &host, int port);
    void setProxy(const QNetworkProxy &proxy) { m_proxy = proxy; }
    void close();
    bool isConnected() const;
    /// Sends one MTProto packet (a whole number of 4-byte words).
    void sendPacket(const QByteArray &payload);

signals:
    void connected();
    /// The link is gone, with the reason; emitted once per connection.
    void disconnected(const QString &reason);
    void packetReceived(const QByteArray &packet);

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onDisconnected();
    void onConnectTimeout();

private:
    void fail(const QString &reason);
    // Manual SOCKS5 (RFC 1928/1929): Qt's own proxy layer does not do username/password
    // auth on Symbian, so the handshake is driven here over a plain socket to the proxy.
    void sendSocksGreeting();
    void sendSocksAuth();
    void sendSocksConnect();
    void processSocks();
    QTcpSocket *m_socket;
    QTimer *m_connectTimer;
    QByteArray m_inbuf;
    bool m_tagSent;
    bool m_open;
    QNetworkProxy m_proxy;
    bool m_useProxy;
    int m_socks;              // SOCKS5 handshake phase (see the .cpp)
    QString m_targetHost;     // the datacenter we ultimately want, reached through the proxy
    int m_targetPort;
};

#endif // MTPROTOTRANSPORT_H
