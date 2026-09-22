// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One end-to-end (secret) chat: its Diffie-Hellman shared key, the seq-number bookkeeping,
// and the MTProto-2.0 encryption of the messages that travel through it. Secret chats are
// device-local - they exist only on the two devices that created them and are never synced -
// so their whole state is persisted here.
//
// The message layer reuses the MTProto 2.0 key derivation already proven in
// MtprotoSession; what is specific to secret chats is the shared-key exchange, the
// decryptedMessageLayer wrapper with its in/out sequence numbers, and the msg_key
// direction bit (x = 0 for the chat's creator, 8 for the other party).
#ifndef SECRETCHAT_H
#define SECRETCHAT_H

#include <QByteArray>
#include <QDataStream>
#include <QString>

class SecretChat
{
public:
    enum State {
        RequestedByMe,   ///< I asked; waiting for the other side to accept (g_a sent)
        RequestedToMe,   ///< the other side asked; I can accept (their g_a stored)
        Ready,           ///< key established, messages flow
        Discarded
    };

    SecretChat();

    // -- identity --
    int chatId() const { return m_chatId; }
    qint64 accessHash() const { return m_accessHash; }
    qint64 peerUserId() const { return m_peerUserId; }
    qint64 adminId() const { return m_adminId; }
    bool isCreator() const { return m_isCreator; }
    State state() const { return m_state; }
    void setState(State s) { m_state = s; }
    void setAccess(int chatId, qint64 accessHash) { m_chatId = chatId; m_accessHash = accessHash; }
    qint64 keyFingerprint() const { return m_keyFingerprint; }
    int ttl() const { return m_ttl; }
    int outSeqCount() const { return m_outSeqCount; }
    void setTtl(int seconds) { m_ttl = seconds; }
    /// The SHA-256-based key visualisation both sides compare to be sure of each other.
    QByteArray keyHash() const;

    // -- key exchange (the caller supplies validated g, p from messages.getDhConfig) --
    /// Start a chat I am creating: pick a, return g_a (256 bytes) to send in requestEncryption.
    QByteArray startAsCreator(qint64 peerUserId, int requestRandomId, int g, const QByteArray &p);
    /// Records an incoming request (their g_a and the chat ids) until the user accepts.
    void setIncomingRequest(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId, const QByteArray &gA);
    QByteArray incomingGa() const { return m_incomingGa; }
    /// The other side requested a chat: given their g_a, pick b, compute the key, and return
    /// g_b (256 bytes) to send in acceptEncryption. state becomes Ready.
    QByteArray acceptAsParticipant(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId,
                                   const QByteArray &gA, int g, const QByteArray &p);
    /// I created the chat and the other side accepted: given their g_b and the fingerprint
    /// they computed, finish the key. Returns false if the fingerprints disagree.
    bool finishAsCreator(int chatId, qint64 accessHash, qint64 adminId, qint64 participantId,
                         const QByteArray &gB, qint64 fingerprint);

    // -- messages --
    /// Wraps a DecryptedMessage body in decryptedMessageLayer (with the seq numbers) and
    /// encrypts it into the `data` bytes for messages.sendEncrypted. Consumes one out seq.
    QByteArray encryptMessage(const QByteArray &decryptedMessageBody);
    /// Decrypts an incoming encryptedMessage's bytes into the DecryptedMessage body (the
    /// content of decryptedMessageLayer.message). Advances the in-seq bookkeeping. Throws
    /// TlException on any inconsistency. Sets `outSeqNo` to the sender's out_seq_no.
    QByteArray decryptMessage(const QByteArray &bytes, int &senderOutSeqNo);

    // -- persistence --
    void save(QDataStream &s) const;
    bool load(QDataStream &s);

private:
    QByteArray computeKey(const QByteArray &otherPublic, const QByteArray &myScalar, const QByteArray &p) const;
    void deriveKeys(const QByteArray &msgKey, int x, QByteArray &aesKey, QByteArray &aesIv) const;
    QByteArray msgKey(const QByteArray &plaintext, int x) const;
    static qint64 fingerprintOf(const QByteArray &key);

    int m_chatId;
    qint64 m_accessHash;
    qint64 m_peerUserId;
    qint64 m_adminId;
    qint64 m_participantId;
    bool m_isCreator;
    State m_state;
    int m_requestRandomId;
    QByteArray m_myScalar;     // a or b, kept until the key is finished
    QByteArray m_p;            // the DH prime for this chat (kept until the key is finished)
    QByteArray m_incomingGa;   // an incoming request's g_a, kept until accepted
    QByteArray m_key;          // 256-byte shared key
    qint64 m_keyFingerprint;
    int m_ttl;
    int m_outSeqCount;         // messages I have sent
    int m_inSeqCount;          // messages I have received
    int m_layer;               // the effective layer (min of both sides once known)
};

#endif // SECRETCHAT_H
