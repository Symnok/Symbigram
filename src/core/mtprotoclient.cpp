// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "mtprotoclient.h"
#include "crypto.h"
#include "inflate.h"
#include "mtprotosession.h"
#include "mtprototransport.h"
#include "tlconstructors.h"
#include "tlreader.h"
#include "tlwriter.h"

#include <QTimer>

namespace
{
    const int RequestTimeoutSec = 40;
    const int PingIntervalSec = 30;
    const int PingDisconnectDelaySec = 75;
    /// Nothing at all from the server for this long (pings included) means the socket is
    /// open but dead; Symbian keeps such sockets alive indefinitely.
    const int DeadLinkSec = 100;
    const int MaxFloodWaitSec = 10;
    const int ClockToleranceSec = 15;
    /// Request ids are unique across every client in the process: a session may hold two
    /// connections at once (the QR login migration) and track both in one table.
    quint64 nextRequestId = 1;
}

MtprotoClient::MtprotoClient(QObject *parent)
    : QObject(parent), m_handshake(0), m_session(0), m_port(0), m_connectionInitialised(false), m_ready(false),
      m_lastReceived(0), m_lastPing(0)
{
    m_transport = new MtprotoTransport(this);
    connect(m_transport, SIGNAL(connected()), this, SLOT(onTransportConnected()));
    connect(m_transport, SIGNAL(disconnected(QString)), this, SLOT(onTransportDisconnected(QString)));
    connect(m_transport, SIGNAL(packetReceived(QByteArray)), this, SLOT(onPacket(QByteArray)));
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, SIGNAL(timeout()), this, SLOT(onTick()));
    m_clock.start();
}

MtprotoClient::~MtprotoClient()
{
    close();
    delete m_session;
}

int MtprotoClient::nowSeconds() const { return m_clock.elapsed() / 1000; }

bool MtprotoClient::isReady() const { return m_ready && m_transport->isConnected(); }

AuthKey MtprotoClient::authKey() const { return m_session ? m_session->authKey() : AuthKey(); }
qint64 MtprotoClient::serverSalt() const { return m_session ? m_session->serverSalt() : 0; }
int MtprotoClient::timeOffset() const { return m_session ? m_session->timeOffset() : 0; }

// -- connecting -------------------------------------------------------------------------------------------

void MtprotoClient::connectToDc(const QString &host, int port, const AuthKey &key)
{
    close();
    m_host = host;
    m_port = port;
    delete m_session;
    m_session = key.isValid() ? new MtprotoSession(key) : 0;
    m_connectionInitialised = false;
    m_transport->connectToHost(host, port);
}

void MtprotoClient::close()
{
    m_tick->stop();
    m_ready = false;
    if (m_handshake) { m_handshake->deleteLater(); m_handshake = 0; }
    m_transport->close();
    m_pending.clear();
    m_byMsgId.clear();
    m_acks.clear();
}

void MtprotoClient::onTransportConnected()
{
    m_lastReceived = nowSeconds();
    if (m_session) {
        startSession(m_session->authKey());
        return;
    }
    m_handshake = new AuthKeyHandshake(m_transport, this);
    connect(m_handshake, SIGNAL(finished(AuthKey)), this, SLOT(onHandshakeFinished(AuthKey)));
    connect(m_handshake, SIGNAL(failed(QString)), this, SLOT(onHandshakeFailed(QString)));
    connect(m_handshake, SIGNAL(log(QString)), this, SIGNAL(log(QString)));
    m_handshake->start();
}

void MtprotoClient::onHandshakeFinished(const AuthKey &key)
{
    if (m_handshake) { m_handshake->deleteLater(); m_handshake = 0; }
    delete m_session;
    m_session = new MtprotoSession(key);
    startSession(key);
}

void MtprotoClient::onHandshakeFailed(const QString &reason)
{
    if (m_handshake) { m_handshake->deleteLater(); m_handshake = 0; }
    m_transport->close();
    emit disconnected(tr("key exchange failed: %1").arg(reason));
}

void MtprotoClient::startSession(const AuthKey &)
{
    m_ready = true;
    m_lastPing = nowSeconds();
    m_tick->start();
    emit connected();
}

void MtprotoClient::onTransportDisconnected(const QString &reason)
{
    bool wasReady = m_ready;
    m_ready = false;
    m_tick->stop();
    if (m_handshake) { m_handshake->deleteLater(); m_handshake = 0; }
    failAll(reason);
    Q_UNUSED(wasReady);
    emit disconnected(reason);
}

void MtprotoClient::failAll(const QString &reason)
{
    QList<quint64> ids = m_pending.keys();
    m_pending.clear();
    m_byMsgId.clear();
    for (int i = 0; i < ids.size(); ++i) emit rpcError(ids.at(i), 0, QLatin1String("NETWORK: ") + reason);
}

// -- requests ---------------------------------------------------------------------------------------------

QByteArray MtprotoClient::wrapInitConnection(const QByteArray &query) const
{
    TlWriter init(query.size() + 128);
    init.writeConstructor(Tl::InitConnection)
        .writeInt(0)                              // flags: no proxy, no params
        .writeInt(m_info.apiId)
        .writeString(m_info.deviceModel)
        .writeString(m_info.systemVersion)
        .writeString(m_info.appVersion)
        .writeString(m_info.systemLangCode)
        .writeString(m_info.langPack)
        .writeString(m_info.langCode)
        .writeRaw(query);
    TlWriter outer(init.length() + 8);
    outer.writeConstructor(Tl::InvokeWithLayer).writeInt(Tl::Layer).writeRaw(init.toByteArray());
    return outer.toByteArray();
}

quint64 MtprotoClient::invoke(const QByteArray &body)
{
    Pending p;
    p.requestId = nextRequestId++;
    p.body = body;
    m_pending.insert(p.requestId, p);
    if (!isReady()) {
        // Reported from the event loop rather than from inside invoke(), so the caller has
        // its request id before hearing about it.
        m_pending[p.requestId].retryAt = -1;
        QTimer::singleShot(0, this, SLOT(failQueued()));
        return p.requestId;
    }
    sendPending(m_pending[p.requestId]);
    return p.requestId;
}

void MtprotoClient::failQueued()
{
    if (isReady()) return;
    QList<quint64> ids = m_pending.keys();
    for (int i = 0; i < ids.size(); ++i) {
        if (m_pending.value(ids.at(i)).retryAt != -1) continue;
        m_pending.remove(ids.at(i));
        emit rpcError(ids.at(i), 0, QLatin1String("NETWORK: not connected"));
    }
}

void MtprotoClient::cancel(quint64 requestId)
{
    if (!m_pending.contains(requestId)) return;
    m_byMsgId.remove(m_pending.value(requestId).msgId);
    m_pending.remove(requestId);
}

void MtprotoClient::sendPending(Pending &p)
{
    if (p.msgId) m_byMsgId.remove(p.msgId);
    p.wrapped = !m_connectionInitialised;
    QByteArray payload = p.wrapped ? wrapInitConnection(p.body) : p.body;
    qint64 msgId = 0;
    QByteArray packet = m_session->encrypt(payload, true, msgId);
    p.msgId = msgId;
    p.sentAt = nowSeconds();
    p.retryAt = 0;
    ++p.attempts;
    m_byMsgId.insert(msgId, p.requestId);
    m_transport->sendPacket(packet);
}

void MtprotoClient::requeue(qint64 msgId)
{
    quint64 requestId = m_byMsgId.value(msgId, 0);
    if (!requestId || !m_pending.contains(requestId)) return;
    Pending &p = m_pending[requestId];
    if (p.attempts >= 4) {
        m_byMsgId.remove(msgId);
        m_pending.remove(requestId);
        emit rpcError(requestId, 0, QLatin1String("the server refused the request repeatedly"));
        return;
    }
    sendPending(p);
}

void MtprotoClient::onTick()
{
    if (!isReady()) return;
    const int now = nowSeconds();

    // Requests queued while offline, flood-wait retries, and timeouts.
    QList<quint64> ids = m_pending.keys();
    for (int i = 0; i < ids.size(); ++i) {
        if (!m_pending.contains(ids.at(i))) continue;
        Pending &p = m_pending[ids.at(i)];
        if (p.retryAt == -1) { sendPending(p); continue; }
        if (p.retryAt > 0 && now >= p.retryAt) { sendPending(p); continue; }
        if (p.retryAt == 0 && now - p.sentAt > RequestTimeoutSec) {
            quint64 id = p.requestId;
            m_byMsgId.remove(p.msgId);
            m_pending.remove(id);
            emit rpcError(id, 0, QLatin1String("TIMEOUT"));
        }
    }

    if (now - m_lastReceived > DeadLinkSec) {
        m_transport->close();
        onTransportDisconnected(tr("no response from the server"));
        return;
    }
    if (now - m_lastPing >= PingIntervalSec) sendPing();
}

void MtprotoClient::sendPing()
{
    // Bare (not content-related): no sequence number. disconnect_delay asks the server to
    // drop us if we go quiet, converting a silently dead connection into a detectable one.
    m_lastPing = nowSeconds();
    TlWriter q(24);
    q.writeConstructor(Tl::PingDelayDisconnect).writeLong(qint64(Crypto::randomUInt64())).writeInt(PingDisconnectDelaySec);
    qint64 msgId;
    m_transport->sendPacket(m_session->encrypt(q.toByteArray(), false, msgId));
}

void MtprotoClient::sendAcks()
{
    if (m_acks.isEmpty() || !isReady()) return;
    TlWriter q(16 + m_acks.size() * 8);
    q.writeConstructor(Tl::MsgsAck).writeVectorOfLong(m_acks);
    m_acks.clear();
    qint64 msgId;
    m_transport->sendPacket(m_session->encrypt(q.toByteArray(), false, msgId));
}

// -- receiving --------------------------------------------------------------------------------------------

void MtprotoClient::onPacket(const QByteArray &packet)
{
    m_lastReceived = nowSeconds();
    if (m_handshake) { m_handshake->onPacket(packet); return; }
    if (!m_session) return;
    qint64 msgId = 0;
    int seqNo = 0;
    QByteArray body;
    try {
        body = m_session->decrypt(packet, msgId, seqNo);
    } catch (const TlException &e) {
        emit log(QLatin1String("   undecryptable packet: ") + e.message());
        return;
    }
    try {
        TlReader r(body);
        dispatch(r, msgId, seqNo);
    } catch (const TlException &e) {
        // One malformed message must not take the connection down.
        emit log(QLatin1String("   dispatch error: ") + e.message());
    }
    sendAcks();
}

void MtprotoClient::observeServerTime(qint64 msgId)
{
    // Every server message id carries the server's timestamp; when it disagrees with our
    // clock by more than jitter, re-base on the server rather than trust the phone.
    int serverTime = int(quint64(msgId) >> 32);
    if (serverTime <= 0) return;
    if (qAbs(m_session->driftFrom(serverTime)) < ClockToleranceSec) return;
    m_session->syncTime(serverTime);
    emit log(QString::fromLatin1("   device clock is %1 s off; compensating").arg(-m_session->timeOffset()));
}

void MtprotoClient::dispatch(TlReader &r, qint64 msgId, int seqNo)
{
    observeServerTime(msgId);
    // Content-related server messages (odd seq) want an acknowledgement, or the server
    // keeps re-sending them.
    if (seqNo & 1) m_acks.append(msgId);

    quint32 type = r.readConstructor();
    switch (type) {
    case Tl::MsgContainer: {
        int count = r.readInt();
        for (int i = 0; i < count; ++i) {
            qint64 innerId = r.readLong();
            int innerSeq = r.readInt();
            int len = r.readInt();
            int end = r.position() + len;
            try { dispatch(r, innerId, innerSeq); }
            catch (const TlException &e) { emit log(QLatin1String("   inner dispatch error: ") + e.message()); }
            r.setPosition(end);                   // resync regardless
        }
        return;
    }

    case Tl::RpcResult: {
        qint64 reqMsgId = r.readLong();
        quint64 requestId = m_byMsgId.value(reqMsgId, 0);
        int mark = r.position();
        quint32 inner = r.readConstructor();
        if (inner == Tl::RpcError) {
            int code = r.readInt();
            QString message = r.readString();
            if (!requestId) { emit log(QLatin1String("   rpc_error for an unknown request: ") + message); return; }
            // FLOOD_WAIT_N is "not yet" rather than a refusal; short waits are sat through.
            if (message.startsWith(QLatin1String("FLOOD_WAIT_"))) {
                int seconds = message.mid(11).toInt();
                if (seconds > 0 && seconds <= MaxFloodWaitSec && m_pending.contains(requestId)) {
                    m_pending[requestId].retryAt = nowSeconds() + seconds + 1;
                    m_byMsgId.remove(reqMsgId);
                    emit log(QString::fromLatin1("   flood wait %1 s, retrying").arg(seconds));
                    return;
                }
            }
            m_byMsgId.remove(reqMsgId);
            m_pending.remove(requestId);
            emit rpcError(requestId, code, message);
            return;
        }
        QByteArray payload;
        if (inner == Tl::GzipPacked) {
            payload = Inflate::gunzip(r.readBytes());
        } else {
            r.setPosition(mark);
            payload = r.readRaw(r.remaining());
        }
        if (!requestId) { emit log(QString::fromLatin1("   result for an unknown request %1").arg(quint64(reqMsgId), 16, 16, QLatin1Char('0'))); return; }
        if (m_pending.value(requestId).wrapped) m_connectionInitialised = true;
        m_byMsgId.remove(reqMsgId);
        m_pending.remove(requestId);
        emit rpcResult(requestId, payload);
        return;
    }

    case Tl::GzipPacked: {
        // A pushed message can be compressed too - an updates container carrying a
        // message brings user objects with it and is large enough to compress.
        QByteArray unpacked = Inflate::gunzip(r.readBytes());
        TlReader inner(unpacked);
        dispatch(inner, msgId, 0);
        return;
    }

    case Tl::NewSessionCreated: {
        r.readLong();                            // first_msg_id
        r.readLong();                            // unique_id
        qint64 salt = r.readLong();
        m_session->setServerSalt(salt);
        emit log(QLatin1String("   new_session_created"));
        return;
    }

    case Tl::BadServerSalt: {
        qint64 badMsgId = r.readLong();
        r.readInt();                             // bad_msg_seqno
        int errorCode = r.readInt();
        qint64 newSalt = r.readLong();
        m_session->setServerSalt(newSalt);
        emit log(QString::fromLatin1("   bad_server_salt (%1), corrected").arg(errorCode));
        requeue(badMsgId);
        return;
    }

    case Tl::BadMsgNotification: {
        qint64 badMsgId = r.readLong();
        r.readInt();                             // bad_msg_seqno
        int code = r.readInt();
        // 16/17: our message id is outside the window around the server's clock. 32/33:
        // the sequence numbering has diverged. Both are recoverable by re-sending.
        if (code == 16 || code == 17) {
            m_session->syncTime(int(quint64(msgId) >> 32));
            emit log(QString::fromLatin1("   bad_msg_notification %1, clock re-synced").arg(code));
            requeue(badMsgId);
        } else if (code == 32 || code == 33) {
            m_session->renew();
            m_connectionInitialised = false;
            emit log(QString::fromLatin1("   bad_msg_notification %1, started a new session").arg(code));
            requeue(badMsgId);
        } else {
            quint64 requestId = m_byMsgId.value(badMsgId, 0);
            emit log(QString::fromLatin1("   bad_msg_notification code %1").arg(code));
            if (requestId) {
                m_byMsgId.remove(badMsgId);
                m_pending.remove(requestId);
                emit rpcError(requestId, 0, QString::fromLatin1("BAD_MSG_%1").arg(code));
            }
        }
        return;
    }

    case Tl::MsgsAck:
    case Tl::Pong:
    case Tl::MsgDetailedInfo:
    case Tl::MsgNewDetailedInfo:
    case Tl::MsgsStateInfo:
    case Tl::MsgsAllInfo:
        return;

    default:
        raiseUpdate(r, type);
        return;
    }
}

void MtprotoClient::raiseUpdate(TlReader &r, quint32 type)
{
    if (!TlSchema::isKnown(type)) {
        emit log(QString::fromLatin1("   unknown pushed message 0x%1").arg(type, 8, 16, QLatin1Char('0')));
        return;
    }
    TlObject obj = TlSchema::readBody(r, type);
    emit updateReceived(obj);
}
