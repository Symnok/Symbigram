// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Desktop harness for the protocol core: the crypto self test, the QR encoder dump, and a
// live session driven by commands from a file - the way the protocol was verified against
// Telegram before the phone build.
//
//   symbigram-cli selftest                 crypto / TL / inflate / BigInt vectors
//   symbigram-cli qr <text> [mask]         the QR matrix as lines of 0/1
//   symbigram-cli srp <password> <params>  the SRP proof for tools/check-srp.py
//   symbigram-cli run [session.dat]        connect; QR login (qr.png + the tg:// url) if
//                                          needed; then commands from sgm-cmd.txt in cwd:
//       dialogs | more | history <n> [count] | send <n|self> <text> | read <n>
//       find <query> | mute <n> on|off | clear <n> | logout | quit
#include "crypto.h"
#include "bigint.h"
#include "dh.h"
#include "inflate.h"
#include "qrcode.h"
#include "secretapi.h"
#include "secretchat.h"
#include "srp.h"
#include "telegramservers.h"
#include "telegramsession.h"
#include "tgcredentials.h"
#include "tlconstructors.h"
#include "tlobject.h"
#include "tlreader.h"
#include "tlwriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

static QTextStream out(stdout);

static void say(const QString &s) { out << s << endl; out.flush(); }

// -- self test --------------------------------------------------------------------------------------------

static int failures = 0;

static void check(bool ok, const char *what)
{
    say(QString::fromLatin1(ok ? "  ok    %1" : "  FAIL  %1").arg(QLatin1String(what)));
    if (!ok) ++failures;
}

static QByteArray hex(const char *h) { return QByteArray::fromHex(h); }

static int selfTest()
{
    say(QLatin1String("hashes"));
    check(Crypto::sha1("abc").toHex() == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(abc)");
    check(Crypto::sha256("abc").toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");
    check(Crypto::sha256(QByteArray()).toHex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256('')");
    check(Crypto::sha256(QByteArray(1000, 'a')).toHex() == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", "sha256(1000 a)");
    check(Crypto::sha512("abc").toHex() == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "sha512(abc)");
    check(Crypto::sha512(QByteArray(1000, 'a')).toHex() == "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634fa7b12b47d77b694de488ace8d9a65967dc96df599727d3292a8d9d447709c97", "sha512(1000 a)");
    // RFC 4231 test case 2
    check(Crypto::hmacSha512("Jefe", "what do ya want for nothing?").toHex() == "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737", "hmac-sha512 rfc4231 #2");
    // PBKDF2-HMAC-SHA512("password", "salt", 1, 64) known vector
    check(Crypto::pbkdf2Sha512("password", "salt", 1, 64).toHex() == "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce", "pbkdf2-sha512 (1 iter)");
    check(Crypto::pbkdf2Sha512("password", "salt", 2, 64).toHex() == "e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53cf76cab2868a39b9f7840edce4fef5a82be67335c77a6068e04112754f27ccf4e", "pbkdf2-sha512 (2 iters)");

    say(QLatin1String("aes"));
    QByteArray key = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    QByteArray pt = hex("00112233445566778899aabbccddeeff");
    QByteArray ct = Crypto::aesEncryptBlock(key, pt);
    check(ct.toHex() == "8ea2b7ca516745bfeafc49904b496089", "aes-256 fips-197 encrypt");
    check(Crypto::aesDecryptBlock(key, ct) == pt, "aes-256 fips-197 decrypt");
    // IGE with the FIPS key, zero plaintext; reference from pycryptodome AES-ECB chained by hand
    QByteArray igeKey = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    QByteArray igeIv = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    QByteArray igePlain(32, '\0');
    QByteArray igeCt = Crypto::aesIgeEncrypt(igePlain, igeKey, igeIv);
    check(igeCt.toHex() == "4a7f16441cee6781e8374f261edeb88dc77147ebd5121de8d0fae7762423b6bf", "aes-256 ige encrypt");
    check(Crypto::aesIgeDecrypt(igeCt, igeKey, igeIv) == igePlain, "aes-256 ige decrypt");
    QByteArray big = Crypto::randomBytes(4096);
    check(Crypto::aesIgeDecrypt(Crypto::aesIgeEncrypt(big, igeKey, igeIv), igeKey, igeIv) == big, "aes ige round trip 4 KB");

    say(QLatin1String("bigint"));
    BigInt a = BigInt::fromBytesBE(hex("0123456789abcdef0123456789abcdef0123456789abcdef"));
    BigInt b = BigInt::fromBytesBE(hex("fedcba9876543210fedcba98"));
    check(BigInt::mul(a, b).toBytesBE().toHex() == "0121fa00ad77d742247acc913fca99aad180b8793fca99aad05ebe789252c268ad05ebe8", "mul");
    BigInt q, r;
    BigInt::divMod(a, b, q, r);
    check(q.toBytesBE().toHex() == "01249249249249237ec687d6" && r.toBytesBE().toHex() == "348a7bdb05b1d774f531aadf", "divmod");
    check(BigInt::modPow(BigInt::fromUInt(4), BigInt::fromUInt(13), BigInt::fromUInt(497)).toBytesBE().toHex() == "01bd", "modpow small");   // 4^13 mod 497 = 445
    // 2^(p-1) mod p == 1 for a prime p (Fermat) with the Telegram prime
    BigInt prime = BigInt::fromBytesBE(hex(
        "c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c37"
        "20fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4"
        "a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4"
        "e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b"));
    BigInt pMinus1 = BigInt::sub(prime, BigInt::one());
    QTime t;
    t.start();
    BigInt fermat = BigInt::modPow(BigInt::fromUInt(2), pMinus1, prime);
    int modpowMs = t.elapsed();
    check(BigInt::compare(fermat, BigInt::one()) == 0, "2^(p-1) mod p == 1 (2048-bit modpow)");
    say(QString::fromLatin1("  2048-bit modpow: %1 ms").arg(modpowMs));
    t.start();
    bool prime2 = DhValidation::isProbablePrime(BigInt::fromUInt(1000003)) && !DhValidation::isProbablePrime(BigInt::fromUInt(1000001));
    check(prime2, "miller-rabin small");
    check(BigInt::fromBytesBE(hex("00000001")).toBytesBE(4).toHex() == "00000001" && BigInt::fromUInt(0x1234).toBytesBE().toHex() == "1234", "byte conversions");

    say(QLatin1String("pq"));
    quint64 p1, q1;
    PqFactorization::factor(Q_UINT64_C(1724114033281923457), p1, q1);
    check(p1 == Q_UINT64_C(1229739323) && q1 == Q_UINT64_C(1402015859), "factor 1724114033281923457");
    PqFactorization::factor(Q_UINT64_C(2986732942540444289), p1, q1);
    check(p1 == Q_UINT64_C(1621103371) && q1 == Q_UINT64_C(1842407459), "factor 2986732942540444289");

    say(QLatin1String("rsa keys"));
    QList<RsaKey> keys = TelegramServers::publicKeys();
    check(keys.size() == 4 && quint64(keys.at(0).fingerprint()) == Q_UINT64_C(0xc3b42b026ce86b21)
          && quint64(keys.at(1).fingerprint()) == Q_UINT64_C(0x9a996a1db11c729b)
          && quint64(keys.at(2).fingerprint()) == Q_UINT64_C(0xb05b2a6f70cdea78)
          && quint64(keys.at(3).fingerprint()) == Q_UINT64_C(0x71e025b6c76033e3), "fingerprints derived from the PEMs");

    say(QLatin1String("tl"));
    TlWriter w;
    w.writeInt(-2).writeLong(Q_INT64_C(0x1122334455667788)).writeBytes("hello").writeBytes(QByteArray(300, 'x')).writeString(QString::fromUtf8("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82")).writeBool(true).writeDouble(1.5);
    TlReader rd(w.toByteArray());
    bool tlOk = rd.readInt() == -2 && rd.readLong() == Q_INT64_C(0x1122334455667788) && rd.readBytes() == "hello"
             && rd.readBytes() == QByteArray(300, 'x') && rd.readString() == QString::fromUtf8("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82") && rd.readBool() && rd.readDouble() == 1.5 && rd.atEnd();
    check(tlOk, "writer/reader round trip with padding");
    check(w.length() % 4 == 0, "4-byte alignment");
    // schema walk: nearestDc#8e1a1775 country:string this_dc:int nearest_dc:int
    TlWriter nd;
    nd.writeConstructor(Tl::NearestDc).writeString(QLatin1String("NL")).writeInt(2).writeInt(4);
    TlReader ndr(nd.toByteArray());
    TlObject o = TlSchema::readObject(ndr);
    check(o.ctor() == Tl::NearestDc && o.str("country") == QLatin1String("NL") && o.intOr("this_dc") == 2 && o.intOr("nearest_dc") == 4, "schema-driven read");
    // conditional fields: auth.loginToken#629f1980 expires:int token:bytes, inside a
    // message#7600b9d3 walk we test flags via updateShortMessage
    TlWriter usm;
    usm.writeConstructor(Tl::UpdateShortMessage).writeInt(2).writeInt(77).writeLong(12345).writeString(QLatin1String("hi")).writeInt(10).writeInt(1).writeInt(1700000000);
    TlReader usmr(usm.toByteArray());
    TlObject u = TlSchema::readObject(usmr);
    TgMessage m = TgApi::readShortMessage(u, 999);
    check(m.out && m.id == 77 && m.peer.id == 12345 && m.fromId == 999 && m.text == QLatin1String("hi") && !u.has("fwd_from"), "flags and absent fields");
    check(TlSchema::isKnown(Tl::Message) && TlSchema::isKnown(Tl::Updates) && !TlSchema::isKnown(0x12345678), "schema table lookups");

    say(QLatin1String("inflate"));
    // gzip of "hello hello hello hello" made with python gzip (mtime 0)
    QByteArray gz = hex("1f8b08000000000002ffcb48cdc9c957c8402701e3513d8d17000000");
    bool gzOk = false;
    try { gzOk = Inflate::gunzip(gz) == "hello hello hello hello"; } catch (const TlException &e) { say(QLatin1String("    ") + e.message()); }
    check(gzOk, "gunzip fixed/dynamic block");
    QFile big2(QLatin1String("selftest.gz"));
    if (big2.open(QIODevice::ReadOnly)) {
        QByteArray data = big2.readAll();
        bool ok = false;
        try { ok = Crypto::sha256(Inflate::gunzip(data)).toHex() == "47a3ea647081fe831890e43b24fa567bb90cffe3ae8755b75b9edebfc32f0e9b"; } catch (const TlException &e) { say(QLatin1String("    ") + e.message()); }
        check(ok, "gunzip selftest.gz (126 KB, dynamic blocks)");
    }

    say(QLatin1String("qr"));
    QVector<bool> modules;
    int size = 0;
    check(QrCode::encode(QLatin1String("tg://login?token=AQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyA"), modules, size) && size == 33, "encode login url (version 4)");


    say(QLatin1String("secret chats"));
    {
        // The Telegram built-in 2048-bit safe prime, g = 3. Simulate both ends of a secret
        // chat locally: creator A and participant B exchange keys and messages.
        QByteArray p = hex(
            "c71caeb9c6b1c9048e6c522f70f13f73980d40238e3e21c14934d037563d930f48198a0aa7c14058229493d22530f4dbfa336f6e0ac925139543aed44cce7c37"
            "20fd51f69458705ac68cd4fe6b6b13abdc9746512969328454f18faf8c595f642477fe96bb2a941d5bcd1d4ac8cc49880708fa9b378e3c4f3a9060bee67cf9a4"
            "a4a695811051907e162753b56b0f6b410dba74d8a84b2a14b3144e0ef1284754fd17ed950d5965b4b9dd46582db1178d169c6bc465b0d6ff9ca3928fef5b9ae4"
            "e418fc15e83ebea0f87fa9ff5eed70050ded2849f47bf959d956850ce929851f0d8115f635b105ee2e4e15d04b2454bf6f4fadf034b10403119cd8e3b92fcc5b");
        int g = 3;
        SecretChat a, b;
        QByteArray gA = a.startAsCreator(2000, 42, g, p);
        QByteArray gB = b.acceptAsParticipant(7, 99, 1000, 2000, gA, g, p);
        bool ok = a.finishAsCreator(7, 99, 1000, 2000, gB, b.keyFingerprint());
        check(ok && a.keyFingerprint() == b.keyFingerprint(), "DH key exchange agrees on the key");

        // A (creator) sends; B decrypts. The creator out_seq_no is odd, so the first one is 1.
        int seq = -1;
        QByteArray enc = a.encryptMessage(SecretApi::textMessage(11, 0, QString::fromUtf8("hello secret \xd0\xbc\xd0\xb8\xd1\x80")));
        SecretContent c1 = SecretApi::read(b.decryptMessage(enc, seq));
        check(c1.kind == SecretContent::Text && c1.text == QString::fromUtf8("hello secret \xd0\xbc\xd0\xb8\xd1\x80") && seq == 1, "creator -> participant text, out_seq 1");

        // B (participant) replies; A decrypts. The participant out_seq_no is even, so first is 0.
        QByteArray enc2 = b.encryptMessage(SecretApi::textMessage(12, 0, QLatin1String("reply back")));
        SecretContent c2 = SecretApi::read(a.decryptMessage(enc2, seq));
        check(c2.kind == SecretContent::Text && c2.text == QLatin1String("reply back") && seq == 0, "participant -> creator text, out_seq 0");

        // A second creator message advances the odd out_seq to 3.
        QByteArray enc3 = a.encryptMessage(SecretApi::textMessage(13, 0, QLatin1String("second")));
        SecretContent c3 = SecretApi::read(b.decryptMessage(enc3, seq));
        check(c3.text == QLatin1String("second") && seq == 3, "creator second message, out_seq 3");

        // A tampered ciphertext must be rejected.
        QByteArray bad = enc3;
        bad[bad.size() - 1] = bad.at(bad.size() - 1) ^ 0x01;
        bool rejected = false;
        try { int s2; b.decryptMessage(bad, s2); } catch (const TlException &) { rejected = true; }
        check(rejected, "tampered secret message rejected");

        // A service action round-trips.
        SecretContent c4 = SecretApi::read(SecretApi::read(SecretApi::notifyLayer(1, 73)).kind == SecretContent::NotifyLayer
                                           ? SecretApi::notifyLayer(1, 73) : QByteArray());
        check(SecretApi::read(SecretApi::notifyLayer(1, 73)).kind == SecretContent::NotifyLayer
              && SecretApi::read(SecretApi::notifyLayer(1, 73)).layer == 73, "notifyLayer service message");
        Q_UNUSED(c4);
    }

    say(failures ? QString::fromLatin1("%1 FAILURE(S)").arg(failures) : QLatin1String("all passed"));
    return failures ? 1 : 0;
}

static int dumpQr(const QString &text, int mask)
{
    QVector<bool> modules;
    int size = 0;
    if (!QrCode::encode(text, modules, size, mask)) { say(QLatin1String("too long")); return 1; }
    for (int r = 0; r < size; ++r) {
        QString line;
        for (int c = 0; c < size; ++c) line += modules.at(r * size + c) ? QLatin1Char('1') : QLatin1Char('0');
        say(line);
    }
    return 0;
}

static void writeQrPng(const QString &url, const QString &path)
{
    QVector<bool> modules;
    int size = 0;
    if (!QrCode::encode(url, modules, size)) return;
    const int quiet = 4, scale = 6;
    int pixels = (size + 2 * quiet) * scale;
    QImage img(pixels, pixels, QImage::Format_RGB32);
    img.fill(0xffffffff);
    for (int r = 0; r < size; ++r)
        for (int c = 0; c < size; ++c)
            if (modules.at(r * size + c))
                for (int y = 0; y < scale; ++y)
                    for (int x = 0; x < scale; ++x)
                        img.setPixel((c + quiet) * scale + x, (r + quiet) * scale + y, 0xff000000);
    img.save(path);
}

// -- live harness --------------------------------------------------------------------------------------

class Harness : public QObject
{
    Q_OBJECT
public:
    Harness(const QString &sessionFile) : m_session(new TelegramSession(this))
    {
        ClientInfo info;
        info.apiId = TG_API_ID;
        info.apiHash = QLatin1String(TG_API_HASH);
        info.deviceModel = QLatin1String("PC (Symbigram harness)");
        info.systemVersion = QLatin1String("Windows");
        info.appVersion = QLatin1String("Symbigram cli");
        info.systemLangCode = QLatin1String("en");
        info.langCode = QLatin1String("en");
        m_session->setClientInfo(info);
        m_session->setSessionFile(sessionFile);

        connect(m_session, SIGNAL(stateChanged()), this, SLOT(onState()));
        connect(m_session, SIGNAL(disconnected(QString)), this, SLOT(onDisconnected(QString)));
        connect(m_session, SIGNAL(qrChanged()), this, SLOT(onQr()));
        connect(m_session, SIGNAL(passwordNeededChanged()), this, SLOT(onPasswordNeeded()));
        connect(m_session, SIGNAL(loginError(QString)), this, SLOT(onLoginError(QString)));
        connect(m_session, SIGNAL(signedIn()), this, SLOT(onSignedIn()));
        connect(m_session, SIGNAL(signedOut(QString)), this, SLOT(onSignedOut(QString)));
        connect(m_session, SIGNAL(selfChanged()), this, SLOT(onSelf()));
        connect(m_session, SIGNAL(dialogsChanged()), this, SLOT(onDialogs()));
        connect(m_session, SIGNAL(dialogChanged(TgPeer)), this, SLOT(onDialog(TgPeer)));
        connect(m_session, SIGNAL(historyLoaded(TgPeer,QList<TgMessage>,int,bool)), this, SLOT(onHistory(TgPeer,QList<TgMessage>,int,bool)));
        connect(m_session, SIGNAL(historyFailed(TgPeer,QString)), this, SLOT(onHistoryFailed(TgPeer,QString)));
        connect(m_session, SIGNAL(messageReceived(TgMessage)), this, SLOT(onMessage(TgMessage)));
        connect(m_session, SIGNAL(messageEdited(TgMessage)), this, SLOT(onEdited(TgMessage)));
        connect(m_session, SIGNAL(messageSent(TgPeer,qint64,TgMessage)), this, SLOT(onSent(TgPeer,qint64,TgMessage)));
        connect(m_session, SIGNAL(downloadProgress(int,qint64,qint64)), this, SLOT(onDlProgress(int,qint64,qint64)));
        connect(m_session, SIGNAL(downloadFinished(int,QString)), this, SLOT(onDlDone(int,QString)));
        connect(m_session, SIGNAL(downloadFailed(int,QString)), this, SLOT(onDlFailed(int,QString)));
        connect(m_session, SIGNAL(uploadProgress(qint64,qint64,qint64)), this, SLOT(onUpProgress(qint64,qint64,qint64)));
        connect(m_session, SIGNAL(messageFailed(TgPeer,qint64,QString)), this, SLOT(onSendFailed(TgPeer,qint64,QString)));
        connect(m_session, SIGNAL(typing(TgPeer,qint64)), this, SLOT(onTyping(TgPeer,qint64)));
        connect(m_session, SIGNAL(readOutbox(TgPeer,int)), this, SLOT(onReadOutbox(TgPeer,int)));
        connect(m_session, SIGNAL(peerResolved(TgPeer)), this, SLOT(onResolved(TgPeer)));
        connect(m_session, SIGNAL(resolveFailed(QString)), this, SLOT(onResolveFailed(QString)));
        connect(m_session, SIGNAL(secretChatRequested(int,qint64)), this, SLOT(onSecretRequested(int,qint64)));
        connect(m_session, SIGNAL(secretChatReady(int)), this, SLOT(onSecretReady(int)));
        connect(m_session, SIGNAL(secretChatDiscarded(int)), this, SLOT(onSecretDiscarded(int)));
        connect(m_session, SIGNAL(secretMessageReceived(int,qint64,QString,int,bool,int)), this, SLOT(onSecretMessage(int,qint64,QString,int,bool,int)));
        connect(m_session, SIGNAL(secretMessageSent(int,qint64,int)), this, SLOT(onSecretSent(int,qint64,int)));
        connect(m_session, SIGNAL(secretMessageFailed(int,qint64,QString)), this, SLOT(onSecretFailed(int,qint64,QString)));
        connect(m_session, SIGNAL(secretMessageExpired(int,qint64)), this, SLOT(onSecretExpired(int,qint64)));
        connect(m_session, SIGNAL(notice(QString)), this, SLOT(onNotice(QString)));
        connect(m_session, SIGNAL(log(QString)), this, SLOT(onLog(QString)));

        m_cmdTimer = new QTimer(this);
        m_cmdTimer->setInterval(1000);
        connect(m_cmdTimer, SIGNAL(timeout()), this, SLOT(pollCommands()));
        m_cmdTimer->start();
        m_session->connectToServer();
    }

private slots:
    void onState()
    {
        static const char *names[] = { "Disconnected", "Connecting", "LoggingIn", "Syncing", "Online" };
        say(QLatin1String("[state] ") + QLatin1String(names[m_session->state()]));
        if (m_session->state() == TelegramSession::Online) m_session->setOnline(true);
    }
    void onDisconnected(const QString &r) { say(QLatin1String("[disconnected] ") + r + QLatin1String(" - reconnecting in 5 s")); QTimer::singleShot(5000, this, SLOT(reconnect())); }
    void reconnect() { if (m_session->state() == TelegramSession::Disconnected) m_session->connectToServer(); }
    void onQr()
    {
        if (m_session->qrUrl().isEmpty()) return;
        say(QLatin1String("[qr] scan with Telegram (Settings > Devices > Link Desktop Device): ") + m_session->qrUrl());
        say(QString::fromLatin1("[qr] expires in %1 s; written to qr.png").arg(m_session->qrExpires() - QDateTime::currentDateTime().toTime_t()));
        writeQrPng(m_session->qrUrl(), QLatin1String("qr.png"));
    }
    void onPasswordNeeded() { say(QLatin1String("[login] two-step verification: put 'password <pw>' into sgm-cmd.txt (hint: ") + m_session->passwordHint() + QLatin1String(")")); }
    void onLoginError(const QString &e) { say(QLatin1String("[login error] ") + e); }
    void onSignedIn() { say(QLatin1String("[signed in]")); }
    void onSignedOut(const QString &r) { say(QLatin1String("[signed out] ") + r); }
    void onSelf() { say(QString::fromLatin1("[self] %1 (%2)").arg(m_session->selfName()).arg(m_session->selfId())); }
    void onDialogs() { say(QString::fromLatin1("[dialogs] %1 chats%2").arg(m_session->dialogs().size()).arg(m_session->dialogsHaveMore() ? QLatin1String(", more available") : QString())); }
    void onDialog(const TgPeer &p) { const TgDialog d = m_session->dialog(p); say(QString::fromLatin1("[dialog] %1 unread=%2 muted=%3").arg(m_session->peers().title(p)).arg(d.unreadCount).arg(d.isMuted(now()))); }
    void onHistory(const TgPeer &p, const QList<TgMessage> &msgs, int offsetId, bool more)
    {
        say(QString::fromLatin1("[history] %1: %2 messages (offset %3, more=%4)").arg(m_session->peers().title(p)).arg(msgs.size()).arg(offsetId).arg(more));
        m_lastHistory = msgs;
        for (int i = msgs.size() - 1; i >= 0; --i) say(QLatin1String("   ") + format(msgs.at(i)));
    }
    void onHistoryFailed(const TgPeer &p, const QString &e) { say(QLatin1String("[history failed] ") + m_session->peers().title(p) + QLatin1String(": ") + e); }
    void onMessage(const TgMessage &m) { say(QLatin1String("[message] ") + m_session->peers().title(m.peer) + QLatin1String(": ") + format(m)); }
    void onEdited(const TgMessage &m) { say(QLatin1String("[edited] ") + format(m)); }
    void onSent(const TgPeer &p, qint64 rid, const TgMessage &m) { say(QString::fromLatin1("[sent] to %1 random=%2 %3").arg(m_session->peers().title(p)).arg(rid).arg(format(m))); }
    void onDlProgress(int job, qint64 got, qint64 total) { say(QString::fromLatin1("[download %1] %2 / %3").arg(job).arg(got).arg(total)); }
    void onDlDone(int job, const QString &path) { say(QString::fromLatin1("[download %1] done: %2").arg(job).arg(path)); }
    void onDlFailed(int job, const QString &e) { say(QString::fromLatin1("[download %1] failed: %2").arg(job).arg(e)); }
    void onUpProgress(qint64 rid, qint64 sent, qint64 total) { say(QString::fromLatin1("[upload %1] %2 / %3").arg(rid).arg(sent).arg(total)); }
    void onSendFailed(const TgPeer &p, qint64 rid, const QString &e) { say(QString::fromLatin1("[send failed] to %1 random=%2: %3").arg(m_session->peers().title(p)).arg(rid).arg(e)); }
    void onTyping(const TgPeer &p, qint64 user) { say(QString::fromLatin1("[typing] %1 in %2").arg(m_session->peers().userName(user)).arg(m_session->peers().title(p))); }
    void onReadOutbox(const TgPeer &p, int maxId) { say(QString::fromLatin1("[read by peer] %1 up to %2").arg(m_session->peers().title(p)).arg(maxId)); }
    void onResolved(const TgPeer &p) { say(QString::fromLatin1("[resolved] %1 -> %2 (%3)").arg(m_session->peers().title(p)).arg(p.key()).arg(p.accessHash)); }
    void onResolveFailed(const QString &e) { say(QLatin1String("[resolve failed] ") + e); }
    void onSecretRequested(int id, qint64 userId) { say(QString::fromLatin1("[secret] incoming request, chat %1 from user %2 - 'acceptsecret %1' to accept").arg(id).arg(userId)); }
    void onSecretReady(int id) { say(QString::fromLatin1("[secret] chat %1 is ready (key hash %2)").arg(id).arg(QString::fromLatin1(m_session->secretChat(id).keyHash.left(8).toHex()))); }
    void onSecretDiscarded(int id) { say(QString::fromLatin1("[secret] chat %1 discarded").arg(id)); }
    void onSecretMessage(int id, qint64 rid, const QString &text, int date, bool out, int ttl) { Q_UNUSED(ttl); say(QString::fromLatin1("[secret %1] %2 %3: %4").arg(id).arg(QDateTime::fromTime_t(date).toString(QLatin1String("HH:mm:ss"))).arg(out ? QLatin1String("me") : QLatin1String("them")).arg(text)); Q_UNUSED(rid); }
    void onSecretSent(int id, qint64 rid, int date) { say(QString::fromLatin1("[secret %1] sent (random %2, date %3)").arg(id).arg(rid).arg(date)); }
    void onSecretFailed(int id, qint64 rid, const QString &e) { say(QString::fromLatin1("[secret %1] send failed: %2").arg(id).arg(e)); Q_UNUSED(rid); }
    void onSecretExpired(int id, qint64 rid) { say(QString::fromLatin1("[secret %1] message %2 self-destructed").arg(id).arg(rid)); }
    void onNotice(const QString &n) { say(QLatin1String("[notice] ") + n); }
    void onLog(const QString &l) { say(QLatin1String("  . ") + l); }

    void pollCommands()
    {
        QFile f(QLatin1String("sgm-cmd.txt"));
        if (!f.exists() || f.size() == 0) return;
        if (!f.open(QIODevice::ReadWrite)) return;
        QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), QString::SkipEmptyParts);
        f.resize(0);
        f.close();
        for (int i = 0; i < lines.size(); ++i) command(lines.at(i).trimmed());
    }

private:
    static int now() { return int(QDateTime::currentDateTime().toTime_t()); }

    QString format(const TgMessage &m) const
    {
        QString who = m.out ? QLatin1String("me") : m_session->peers().userName(m.fromId);
        QString text = m.text;
        if (!m.note.isEmpty()) text += QLatin1String(" [") + m.note + QLatin1Char(']');
        if (m.media.isValid()) text += QString::fromLatin1(" {media id=%1 dc=%2 size=%3 show=%4 big=%5 %6}").arg(m.media.id).arg(m.media.dcId).arg(m.media.fileSize).arg(m.media.sizeType).arg(m.media.bigSizeType).arg(m.media.fileName);
        if (!m.forwardedFrom.isEmpty()) text += QLatin1String(" (fwd from ") + m.forwardedFrom + QLatin1Char(')');
        if (m.replyToId) text += QString::fromLatin1(" (reply to %1)").arg(m.replyToId);
        return QString::fromLatin1("#%1 %2 %3: %4").arg(m.id).arg(QDateTime::fromTime_t(m.date).toString(QLatin1String("dd.MM HH:mm"))).arg(who).arg(text);
    }

    TgPeer peerAt(const QString &arg) const
    {
        if (arg == QLatin1String("self")) return TgPeer(TgPeer::User, m_session->selfId());
        int n = arg.toInt();
        if (n < 0 || n >= m_session->dialogs().size()) return TgPeer();
        return m_session->dialogs().at(n).peer;
    }

    void command(const QString &line)
    {
        say(QLatin1String("> ") + line);
        QStringList a = line.split(QLatin1Char(' '), QString::SkipEmptyParts);
        if (a.isEmpty()) return;
        QString cmd = a.at(0);
        if (cmd == QLatin1String("quit")) { m_session->disconnectFromServer(); qApp->quit(); }
        else if (cmd == QLatin1String("dialogs")) {
            const QList<TgDialog> &d = m_session->dialogs();
            for (int i = 0; i < d.size(); ++i) {
                const TgPeerInfo info = m_session->peers().info(d.at(i).peer);
                say(QString::fromLatin1("  %1. %2%3 [%4] unread=%5 top=%6 %7%8").arg(i).arg(info.title).arg(d.at(i).pinned ? QLatin1String(" (pinned)") : QString())
                    .arg(d.at(i).peer.key()).arg(d.at(i).unreadCount).arg(d.at(i).topMessageId)
                    .arg(d.at(i).isMuted(now()) ? QLatin1String("(muted) ") : QString()).arg(d.at(i).lastText.left(60)));
            }
        }
        else if (cmd == QLatin1String("more")) m_session->loadMoreDialogs();
        else if (cmd == QLatin1String("folders")) {
            const QList<TgFolder> &f = m_session->folders();
            say(QString::fromLatin1("[folders] %1 custom folders").arg(f.size()));
            for (int i = 0; i < f.size(); ++i)
                say(QString::fromLatin1("  %1. %2  include=%3 pinned=%4 exclude=%5 cats(c%6 nc%7 g%8 b%9 bot%10) exArch=%11")
                    .arg(i).arg(f.at(i).title).arg(f.at(i).include.size()).arg(f.at(i).pinned.size()).arg(f.at(i).exclude.size())
                    .arg(f.at(i).contacts).arg(f.at(i).nonContacts).arg(f.at(i).groups).arg(f.at(i).broadcasts).arg(f.at(i).bots).arg(f.at(i).excludeArchived));
        }
        else if (cmd == QLatin1String("archive")) {
            const QList<TgDialog> &a = m_session->archivedDialogs();
            say(QString::fromLatin1("[archive] %1 chats%2").arg(a.size()).arg(m_session->archiveHasMore() ? QLatin1String(" (more)") : QString()));
            for (int i = 0; i < a.size(); ++i)
                say(QString::fromLatin1("  %1. %2  unread=%3").arg(i).arg(m_session->peers().title(a.at(i).peer)).arg(a.at(i).unreadCount));
        }
        else if (cmd == QLatin1String("refresh")) m_session->refreshDialogs();
        else if (cmd == QLatin1String("history") && a.size() >= 2) m_session->loadHistory(peerAt(a.at(1)), a.size() >= 4 ? a.at(3).toInt() : 0, a.size() >= 3 ? a.at(2).toInt() : 20);
        else if (cmd == QLatin1String("send") && a.size() >= 3) m_session->sendText(peerAt(a.at(1)), QStringList(a.mid(2)).join(QLatin1String(" ")));
        else if (cmd == QLatin1String("read") && a.size() >= 2) { TgDialog d = m_session->dialog(peerAt(a.at(1))); m_session->markRead(d.peer, d.topMessageId); }
        else if (cmd == QLatin1String("typing") && a.size() >= 2) m_session->setTyping(peerAt(a.at(1)), true);
        else if (cmd == QLatin1String("find") && a.size() >= 2) m_session->resolve(QStringList(a.mid(1)).join(QLatin1String(" ")));
        else if (cmd == QLatin1String("mute") && a.size() >= 3) m_session->setMuted(peerAt(a.at(1)), a.at(2) == QLatin1String("on"));
        else if (cmd == QLatin1String("clear") && a.size() >= 2) m_session->deleteHistory(peerAt(a.at(1)));
        else if (cmd == QLatin1String("password") && a.size() >= 2) m_session->checkPassword(QStringList(a.mid(1)).join(QLatin1String(" ")));
        else if (cmd == QLatin1String("logout")) m_session->logOut();
        else if (cmd == QLatin1String("secret") && a.size() >= 2) { TgPeer p = peerAt(a.at(1)); if (p.kind == TgPeer::User) m_session->requestSecretChat(p); else say(QLatin1String("secret chats need a user; open the dialog first")); }
        else if (cmd == QLatin1String("secrets")) { QList<TgSecretChat> sc = m_session->secretChats(); say(QString::fromLatin1("[secrets] %1").arg(sc.size())); for (int i = 0; i < sc.size(); ++i) say(QString::fromLatin1("  chat %1 user %2 state %3 creator %4 ttl %5").arg(sc.at(i).id).arg(sc.at(i).peerUserId).arg(sc.at(i).state).arg(sc.at(i).isCreator).arg(sc.at(i).ttl)); }
        else if (cmd == QLatin1String("acceptsecret") && a.size() >= 2) m_session->acceptSecretChat(a.at(1).toInt());
        else if (cmd == QLatin1String("secretsend") && a.size() >= 3) m_session->sendSecretText(a.at(1).toInt(), QStringList(a.mid(2)).join(QLatin1String(" ")));
        else if (cmd == QLatin1String("secretttl") && a.size() >= 3) m_session->setSecretTtl(a.at(1).toInt(), a.at(2).toInt());
        else if (cmd == QLatin1String("discardsecret") && a.size() >= 2) m_session->discardSecretChat(a.at(1).toInt());
        else if (cmd == QLatin1String("del") && a.size() >= 3) m_session->deleteMessages(peerAt(a.at(1)), QList<int>() << a.at(2).toInt(), a.value(3) == QLatin1String("all"));
        else if (cmd == QLatin1String("get") && a.size() >= 2) {
            // get <message id> [size]: downloads the attachment of a message shown by history
            for (int i = 0; i < m_lastHistory.size(); ++i)
                if (m_lastHistory.at(i).id == a.at(1).toInt() && m_lastHistory.at(i).media.isValid()) {
                    const TgMedia &md = m_lastHistory.at(i).media;
                    QString size = a.size() >= 3 ? a.at(2) : (md.kind == TgMedia::Photo ? md.sizeType : QString());
                    QString name = QString::fromLatin1("dl_%1_%2.%3").arg(md.id).arg(size).arg(md.kind == TgMedia::Photo ? QLatin1String("jpg") : QLatin1String("bin"));
                    say(QString::fromLatin1("[download] job %1 -> %2").arg(m_session->downloadFile(md, size, name)).arg(name));
                }
        }
        else if (cmd == QLatin1String("avatar") && a.size() >= 2) {
            TgPeer p = peerAt(a.at(1));
            TgPeerInfo info = m_session->peers().info(p);
            say(QString::fromLatin1("[avatar] photo id %1 dc %2 job %3").arg(info.photoId).arg(info.photoDcId)
                .arg(info.photoId ? m_session->downloadPeerPhoto(p, info.photoId, info.photoDcId, QString::fromLatin1("avatar_%1.jpg").arg(p.id)) : 0));
        }
        else if (cmd == QLatin1String("sendfile") && a.size() >= 3) m_session->sendFile(peerAt(a.at(1)), a.at(2), a.value(3) == QLatin1String("photo"), QStringList(a.mid(4)).join(QLatin1String(" ")));
        else if (cmd == QLatin1String("offline")) m_session->setOnline(false);
        else if (cmd == QLatin1String("online")) m_session->setOnline(true);
        else if (cmd == QLatin1String("disconnect")) m_session->disconnectFromServer();
        else if (cmd == QLatin1String("connect")) m_session->connectToServer();
        else say(QLatin1String("unknown command"));
    }

    TelegramSession *m_session;
    QTimer *m_cmdTimer;
    QList<TgMessage> m_lastHistory;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qRegisterMetaType<TlObject>("TlObject");
    qRegisterMetaType<TgPeer>("TgPeer");
    qRegisterMetaType<TgMessage>("TgMessage");
    qRegisterMetaType<QList<TgMessage> >("QList<TgMessage>");
    qRegisterMetaType<AuthKey>("AuthKey");
    QStringList args = app.arguments();
    QString cmd = args.value(1);
    if (cmd == QLatin1String("selftest")) return selfTest();
    if (cmd == QLatin1String("qr")) return dumpQr(args.value(2), args.value(3, QLatin1String("-1")).toInt());
    if (cmd == QLatin1String("run")) {
        Harness h(args.value(2, QLatin1String("session.dat")));
        return app.exec();
    }
    say(QLatin1String("usage: symbigram-cli selftest | qr <text> [mask] | run [session.dat]"));
    return 2;
}

#include "main.moc"
