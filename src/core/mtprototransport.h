// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A TCP connection to one datacenter with MTProto's "intermediate" framing: a one-off
// 0xeeeeeeee tag, then every packet prefixed with its little-endian length. Chosen over the
// abridged format because the length field is a plain uint32 in both directions, with no
// size-dependent special case to get wrong.
//
// Optionally wrapped in an MTProto proxy's obfuscation ("obfuscated2"): a 64-byte init payload
// followed by the entire stream under AES-256-CTR, with "padded intermediate" framing. That is a
// layer below the framing and above TCP, and it composes with the SOCKS5 tunnel below - a proxy
// chain is allowed, and the SOCKS5 negotiation itself stays in the clear.
#ifndef MTPROTOTRANSPORT_H
#define MTPROTOTRANSPORT_H

#include "crypto.h"

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

    /// `dcId` is only used by an MTProto proxy, which wants to know which datacenter to reach;
    /// a direct or SOCKS5 connection ignores it.
    void connectToHost(const QString &host, int port, int dcId);
    void setProxy(const QNetworkProxy &proxy) { m_proxy = proxy; }

    /// Turns on MTProto proxy obfuscation. An empty secret turns it off. 16 bytes is plain
    /// obfuscation; 17 bytes beginning 0xdd selects padded intermediate framing; 17 bytes
    /// beginning 0xee is FakeTLS, which is NOT implemented and is refused at connect time.
    void setObfuscation(const QByteArray &secret) { m_secret = secret; }
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
    /// Everything from here on is MTProto framing: sends the obfuscation init when one is wanted,
    /// then reports the connection up. Shared by the direct and the SOCKS5 paths.
    void beginStream();
    void sendObfuscatedInit();
    /// Writes into the MTProto stream, encrypting first when obfuscated. SOCKS5 bytes must not
    /// go through here.
    void writeStream(const QByteArray &data);
    /// The payload length inside a padded-intermediate frame. The length prefix counts 0-15
    /// random padding bytes and the transport gives no way to tell them apart, so the MTProto
    /// message has to say where it ends; see the .cpp.
    static int unpaddedLength(const QByteArray &frame);
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
    int m_dcId;
    QByteArray m_secret;      // empty unless an MTProto proxy is configured
    bool m_obfuscated;        // this connection is wrapped (set at connect time from m_secret)
    bool m_padded;            // ... with padded intermediate framing (a "dd" secret)
    AesCtr m_enc;
    AesCtr m_dec;
};

#endif // MTPROTOTRANSPORT_H
