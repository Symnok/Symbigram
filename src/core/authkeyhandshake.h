// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Creates a permanent authorisation key by Diffie-Hellman, per MTProto 2.0: three round
// trips (req_pq, req_DH_params, set_client_DH_params). The client proves it did the work by
// factoring pq, the server's DH parameters arrive inside an RSA-encrypted envelope, and
// both sides confirm they derived the same key by exchanging hashes of it.
//
// The key is permanent and is what every later message is encrypted with, so this runs
// once per datacenter and the result is persisted. The two 2048-bit exponentiations are the
// slowest thing the app ever does, so they run in a worker thread and the UI keeps moving.
#ifndef AUTHKEYHANDSHAKE_H
#define AUTHKEYHANDSHAKE_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

class MtprotoTransport;

/// The result of a successful handshake - everything a session needs.
struct AuthKey
{
    AuthKey() : keyId(0), serverSalt(0), timeOffset(0) {}
    bool isValid() const { return key.size() == 256; }
    QByteArray key;          // 256 bytes
    qint64 keyId;
    qint64 serverSalt;
    int timeOffset;          // server time minus ours, in seconds
};

/// The heavy arithmetic of the handshake, off the GUI thread: parameter validation,
/// g^b mod p, and the key g_a^b mod p.
class DhWorker : public QThread
{
    Q_OBJECT
public:
    explicit DhWorker(QObject *parent = 0) : QThread(parent), g(0) {}
    int g;
    QByteArray dhPrime, ga, b;       // inputs
    QByteArray gb, authKey;          // outputs (256 bytes each)
    QString error, note;
protected:
    void run();
};

class AuthKeyHandshake : public QObject
{
    Q_OBJECT
public:
    explicit AuthKeyHandshake(MtprotoTransport *transport, QObject *parent = 0);
    ~AuthKeyHandshake();

    void start();
    bool isRunning() const { return m_stage != Idle && m_stage != Done; }

public slots:
    /// Every packet the transport delivers while the handshake runs.
    void onPacket(const QByteArray &packet);

signals:
    void finished(const AuthKey &key);
    void failed(const QString &reason);
    void log(const QString &line);

private slots:
    void onWorkerDone();

private:
    enum Stage { Idle, SentReqPq, SentReqDh, Computing, SentSetClientDh, Done };
    void send(const QByteArray &body);
    void fail(const QString &reason);
    qint64 nextMessageId();

    MtprotoTransport *m_transport;
    DhWorker *m_worker;
    Stage m_stage;
    qint64 m_lastMsgId;
    QByteArray m_nonce, m_serverNonce, m_newNonce;
    QByteArray m_tmpKey, m_tmpIv;
    int m_timeOffset;
};

#endif // AUTHKEYHANDSHAKE_H
