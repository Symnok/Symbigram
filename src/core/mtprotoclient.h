// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A connection to one datacenter: handshake, encrypted session, request correlation, and
// incoming updates. MTProto replies are not a stream of answers to questions: a packet may
// hold a container of several messages, the server interjects salt corrections and session
// notices, and it pushes updates nobody asked for, at any time. So one receive path owns
// the socket, requests are parked by message id, and everything that is not a reply is
// raised as an update.
#ifndef MTPROTOCLIENT_H
#define MTPROTOCLIENT_H

#include "authkeyhandshake.h"
#include "tlobject.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTime>

class MtprotoTransport;
class MtprotoSession;
class QTimer;

/// What the client tells the server about itself in initConnection.
struct ClientInfo
{
    ClientInfo() : apiId(0) {}
    int apiId;
    QString apiHash;
    QString deviceModel;
    QString systemVersion;
    QString appVersion;
    QString systemLangCode;
    QString langPack;
    QString langCode;
};

class MtprotoClient : public QObject
{
    Q_OBJECT
public:
    explicit MtprotoClient(QObject *parent = 0);
    ~MtprotoClient();

    void setInfo(const ClientInfo &info) { m_info = info; }
    const ClientInfo &info() const { return m_info; }

    /// Connects and, without a valid key, negotiates one. connected() follows.
    void connectToDc(const QString &host, int port, const AuthKey &key = AuthKey());
    void close();
    bool isReady() const;
    AuthKey authKey() const;
    qint64 serverSalt() const;
    int timeOffset() const;
    QString host() const { return m_host; }

    /// Sends a TL request; the answer arrives as rpcResult or rpcError with this id. The
    /// first request on a connection is wrapped in invokeWithLayer/initConnection.
    quint64 invoke(const QByteArray &body);
    /// Forgets a request whose answer is no longer wanted.
    void cancel(quint64 requestId);

signals:
    /// The encrypted session is up; requests can be sent.
    void connected();
    /// The link is gone (transport failure, handshake failure, or close()).
    void disconnected(const QString &reason);
    /// The TL bytes of a result, gunzipped when the server compressed them.
    void rpcResult(quint64 requestId, const QByteArray &result);
    void rpcError(quint64 requestId, int code, const QString &type);
    /// Every Update the server pushes.
    void updateReceived(const TlObject &update);
    void log(const QString &line);

private slots:
    void onTransportConnected();
    void onTransportDisconnected(const QString &reason);
    void onPacket(const QByteArray &packet);
    void onHandshakeFinished(const AuthKey &key);
    void onHandshakeFailed(const QString &reason);
    void onTick();
    void failQueued();

private:
    struct Pending
    {
        Pending() : requestId(0), msgId(0), attempts(0), retryAt(0), wrapped(false) {}
        quint64 requestId;
        QByteArray body;
        qint64 msgId;
        int attempts;
        int sentAt;        // seconds since the client was created
        int retryAt;       // 0, or when to (re)send
        bool wrapped;      // sent inside initConnection
    };

    void startSession(const AuthKey &key);
    void sendPending(Pending &p);
    void requeue(qint64 msgId);
    void dispatch(TlReader &r, qint64 msgId, int seqNo);
    void raiseUpdate(TlReader &r, quint32 type);
    void observeServerTime(qint64 msgId);
    void sendPing();
    void sendAcks();
    void failAll(const QString &reason);
    QByteArray wrapInitConnection(const QByteArray &query) const;
    int nowSeconds() const;

    MtprotoTransport *m_transport;
    AuthKeyHandshake *m_handshake;
    MtprotoSession *m_session;
    QTimer *m_tick;
    ClientInfo m_info;
    QString m_host;
    int m_port;
    bool m_connectionInitialised;
    bool m_ready;
    QTime m_clock;
    int m_lastReceived;
    int m_lastPing;
    QHash<qint64, quint64> m_byMsgId;           // msg id -> request id
    QHash<quint64, Pending> m_pending;          // request id -> request
    QList<qint64> m_acks;
};

#endif // MTPROTOCLIENT_H
