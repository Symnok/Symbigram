// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "telegramsession.h"
#include "secretapi.h"
#include "tlwriter.h"
#include "bigint.h"
#include "crypto.h"
#include "dh.h"
#include "telegramservers.h"
#include "tlconstructors.h"
#include "tlreader.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace
{
    const quint32 SessionMagic = 0x53474d31;   // "SGM1"
    const int QrPollMs = 2500;
    const int DialogPageSize = 40;
    const int SaveIntervalMs = 15000;
    /// Download chunk: must be a multiple of 4 KB dividing 1 MB. Upload part: divides 1 MB.
    const int ChunkSize = 128 * 1024;
    const int PartSize = 128 * 1024;
    const qint64 BigFileThreshold = Q_INT64_C(10) * 1024 * 1024;
    const int MaxActiveDownloads = 3;
}

int TelegramSession::unixNow() { return int(QDateTime::currentDateTime().toTime_t()); }

TelegramSession::TelegramSession(QObject *parent)
    : QObject(parent), m_moved(0), m_srp(0), m_state(Disconnected), m_dcId(TelegramServers::DefaultDc), m_signedIn(false),
      m_stateDirty(false), m_qrExpires(0), m_passwordNeeded(false), m_movedDc(0), m_loggingOut(false),
      m_codeNeeded(false), m_codeLength(0), m_selfId(0),
      m_archiveHasMore(false), m_archiveLoaded(false), m_dialogsHaveMore(false), m_dialogsLoading(false),
      m_differencePending(false), m_online(false), m_nextJobId(1),
      m_dhG(0), m_dhVersion(0), m_dhReady(false)
{
    m_client = new MtprotoClient(this);
    connect(m_client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(m_client, SIGNAL(disconnected(QString)), this, SLOT(onDisconnected(QString)));
    connect(m_client, SIGNAL(rpcResult(quint64,QByteArray)), this, SLOT(onRpcResult(quint64,QByteArray)));
    connect(m_client, SIGNAL(rpcError(quint64,int,QString)), this, SLOT(onRpcError(quint64,int,QString)));
    connect(m_client, SIGNAL(updateReceived(TlObject)), this, SLOT(onUpdate(TlObject)));
    connect(m_client, SIGNAL(log(QString)), this, SIGNAL(log(QString)));

    m_qrPoll = new QTimer(this);
    m_qrPoll->setInterval(QrPollMs);
    connect(m_qrPoll, SIGNAL(timeout()), this, SLOT(onQrPoll()));
    m_saveTimer = new QTimer(this);
    m_saveTimer->setInterval(SaveIntervalMs);
    connect(m_saveTimer, SIGNAL(timeout()), this, SLOT(onSaveTimer()));

    m_secretExpiryTimer = new QTimer(this);
    m_secretExpiryTimer->setInterval(1000);
    connect(m_secretExpiryTimer, SIGNAL(timeout()), this, SLOT(onSecretExpiryTick()));
}

TelegramSession::~TelegramSession()
{
    if (m_srp) { m_srp->wait(); delete m_srp; }
    saveState();
}

void TelegramSession::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged();
}

// -- the stored session -------------------------------------------------------------------------------

void TelegramSession::setSessionFile(const QString &path)
{
    m_sessionFile = path;
    loadSessionFile();
    loadSecrets();
}

void TelegramSession::loadSessionFile()
{
    m_authKey = AuthKey();
    m_signedIn = false;
    m_updateState = TgUpdateState();
    QFile f(m_sessionFile);
    if (!f.open(QIODevice::ReadOnly)) return;
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_4_7);
    quint32 magic = 0;
    s >> magic;
    if (magic != SessionMagic) return;
    quint8 signedIn = 0;
    s >> m_authKey.key >> m_authKey.keyId >> m_authKey.serverSalt >> m_authKey.timeOffset >> m_dcId >> signedIn
      >> m_updateState.pts >> m_updateState.qts >> m_updateState.date >> m_updateState.seq >> m_selfId;
    m_dcKeys.clear();
    if (s.status() == QDataStream::Ok && !s.atEnd()) {
        qint32 count = 0;
        s >> count;
        for (int i = 0; i < count && s.status() == QDataStream::Ok; ++i) {
            qint32 dc = 0;
            AuthKey k;
            s >> dc >> k.key >> k.keyId >> k.serverSalt >> k.timeOffset;
            if (k.isValid()) m_dcKeys.insert(dc, k);
        }
    }
    if (s.status() != QDataStream::Ok || !m_authKey.isValid()) {
        // A half-written file: sign in again rather than crash on every launch.
        m_authKey = AuthKey();
        m_selfId = 0;
        return;
    }
    m_signedIn = signedIn != 0;
    if (m_dcId < 1 || m_dcId > 5) m_dcId = TelegramServers::DefaultDc;
}

void TelegramSession::saveSessionFile()
{
    if (m_sessionFile.isEmpty()) return;
    if (!m_authKey.isValid()) { QFile::remove(m_sessionFile); return; }
    QDir().mkpath(QFileInfo(m_sessionFile).absolutePath());
    QFile f(m_sessionFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_4_7);
    s << SessionMagic << m_authKey.key << m_authKey.keyId << m_authKey.serverSalt << m_authKey.timeOffset << m_dcId
      << quint8(m_signedIn ? 1 : 0) << m_updateState.pts << m_updateState.qts << m_updateState.date << m_updateState.seq << m_selfId;
    s << qint32(m_dcKeys.size());
    for (QHash<int, AuthKey>::const_iterator it = m_dcKeys.constBegin(); it != m_dcKeys.constEnd(); ++it)
        s << qint32(it.key()) << it.value().key << it.value().keyId << it.value().serverSalt << it.value().timeOffset;
    m_stateDirty = false;
}

void TelegramSession::saveState()
{
    if (m_stateDirty) saveSessionFile();
}

void TelegramSession::onSaveTimer()
{
    saveState();
}

void TelegramSession::forgetSession(const QString &reason)
{
    m_qrPoll->stop();
    m_saveTimer->stop();
    m_client->close();
    if (m_moved) { m_moved->deleteLater(); m_moved = 0; }
    m_requests.clear();
    failAllTransfers(tr("Signed out."));
    QList<int> dcs = m_dcLinks.keys();
    for (int i = 0; i < dcs.size(); ++i) m_dcLinks[dcs.at(i)].client->deleteLater();
    m_dcLinks.clear();
    m_dcKeys.clear();
    m_authKey = AuthKey();
    m_signedIn = false;
    m_updateState = TgUpdateState();
    m_selfId = 0;
    m_dialogs.clear();
    m_archived.clear();
    m_archivedPeers.clear();
    m_folders.clear();
    m_archiveLoaded = false;
    qDeleteAll(m_secretChats);
    m_secretChats.clear();
    m_secretHistory.clear();
    m_secretSeen.clear();
    m_secretExpiryTimer->stop();
    m_dhReady = false;
    if (!m_sessionFile.isEmpty()) QFile::remove(secretFilePath());
    m_peers.clear();
    m_qrUrl.clear();
    m_qrToken.clear();
    m_passwordNeeded = false;
    m_loginPhone.clear();
    m_phoneCodeHash.clear();
    m_codeNeeded = false;
    m_codeLength = 0;
    m_stateDirty = false;
    m_loggingOut = false;
    if (!m_sessionFile.isEmpty()) QFile::remove(m_sessionFile);
    setState(Disconnected);
    emit dialogsChanged();
    emit signedOut(reason);
}

// -- connecting ----------------------------------------------------------------------------------------

void TelegramSession::connectToServer()
{
    if (m_state != Disconnected) return;
    m_loggingOut = false;
    m_client->setInfo(m_info);
    setState(Connecting);
    emit log(QString::fromLatin1("connecting to dc%1 (%2 key)").arg(m_dcId).arg(m_authKey.isValid() ? QLatin1String("stored") : QLatin1String("new")));
    m_client->connectToDc(TelegramServers::hostFor(m_dcId), TelegramServers::DefaultPort, m_authKey);
}

void TelegramSession::setProxy(bool enabled, const QString &host, int port, const QString &user, const QString &pass)
{
    if (enabled && !host.isEmpty())
        m_proxy = QNetworkProxy(QNetworkProxy::Socks5Proxy, host, quint16(port), user, pass);
    else
        m_proxy = QNetworkProxy(QNetworkProxy::NoProxy);
    m_client->setProxy(m_proxy);
    if (m_moved) m_moved->setProxy(m_proxy);
    for (QHash<int, DcLink>::const_iterator it = m_dcLinks.constBegin(); it != m_dcLinks.constEnd(); ++it)
        if (it.value().client) it.value().client->setProxy(m_proxy);
}

void TelegramSession::disconnectFromServer()
{
    m_qrPoll->stop();
    m_saveTimer->stop();
    saveState();
    m_requests.clear();
    m_client->close();
    if (m_moved) { m_moved->deleteLater(); m_moved = 0; }
    failAllTransfers(tr("Not connected."));
    m_online = false;
    setState(Disconnected);
}

void TelegramSession::onConnected()
{
    // A fresh key: remember it before anything else, the handshake was the expensive part.
    if (!m_authKey.isValid() || m_authKey.keyId != m_client->authKey().keyId) {
        m_authKey = m_client->authKey();
        saveSessionFile();
    }
    if (m_signedIn) startSync();
    else startLogin();
}

void TelegramSession::onDisconnected(const QString &reason)
{
    m_qrPoll->stop();
    m_saveTimer->stop();
    saveState();
    m_requests.clear();
    m_online = false;
    m_dialogsLoading = false;
    m_differencePending = false;
    failAllTransfers(reason);
    if (m_loggingOut) {
        // logOut's answer never came, or came as the connection closed: the local key
        // goes either way - the user asked to sign out.
        forgetSession(tr("Signed out."));
        return;
    }
    setState(Disconnected);
    emit disconnected(reason);
}

quint64 TelegramSession::send(Kind kind, const QByteArray &body, const Request &req)
{
    return sendOn(m_client, kind, body, req);
}

quint64 TelegramSession::sendOn(MtprotoClient *client, Kind kind, const QByteArray &body, const Request &req)
{
    Request r = req;
    r.kind = kind;
    quint64 id = client->invoke(body);
    m_requests.insert(id, r);
    return id;
}

bool TelegramSession::isAuthGone(const QString &type) const
{
    return type.contains(QLatin1String("AUTH_KEY_UNREGISTERED")) || type.contains(QLatin1String("SESSION_REVOKED"))
        || type.contains(QLatin1String("SESSION_EXPIRED")) || type.contains(QLatin1String("USER_DEACTIVATED"))
        || type.contains(QLatin1String("AUTH_KEY_DUPLICATED")) || type.contains(QLatin1String("AUTH_KEY_INVALID"));
}

void TelegramSession::handleMigrate(const QString &type)
{
    // "USER_MIGRATE_4" and friends: the account lives on another datacenter. A key is per
    // datacenter, so the move means a new handshake there.
    int underscore = type.lastIndexOf(QLatin1Char('_'));
    int dc = type.mid(underscore + 1).toInt();
    if (dc < 1 || dc > 5) return;
    emit log(QString::fromLatin1("migrating to dc%1").arg(dc));
    m_client->close();
    m_requests.clear();
    m_dcId = dc;
    m_authKey = AuthKey();
    setState(Disconnected);
    connectToServer();
}

// -- QR login ----------------------------------------------------------------------------------------------

void TelegramSession::startLogin()
{
    m_passwordNeeded = false;
    setState(LoggingIn);
    // A phone-number login in progress (e.g. resumed after a datacenter migrate) asks for the
    // code again on the datacenter we landed on; otherwise fall back to the QR code.
    if (!m_loginPhone.isEmpty()) sendPhoneCode();
    else exportToken();
}

void TelegramSession::exportToken()
{
    send(ExportToken, TgApi::exportLoginToken(m_info.apiId, m_info.apiHash));
}

void TelegramSession::sendPhoneCode()
{
    m_codeNeeded = false;
    m_phoneCodeHash.clear();
    send(SendCode, TgApi::authSendCode(m_loginPhone, m_info.apiId, m_info.apiHash));
}

void TelegramSession::startPhoneLogin(const QString &phone)
{
    if (m_state != LoggingIn) return;
    QString digits = TgApi::normalisePhone(phone);
    if (digits.size() < 5) { emit loginError(tr("Enter a valid phone number, with the country code.")); return; }
    m_qrPoll->stop();
    if (m_moved) { m_moved->deleteLater(); m_moved = 0; }   // drop any QR migration in flight
    m_loginPhone = digits;
    m_codeNeeded = false;
    m_codeLength = 0;
    m_phoneCodeHash.clear();
    m_qrUrl.clear();
    emit qrChanged();
    sendPhoneCode();
}

void TelegramSession::submitCode(const QString &code)
{
    if (!m_codeNeeded || m_loginPhone.isEmpty() || m_phoneCodeHash.isEmpty()) return;
    QString c = code.trimmed();
    if (c.isEmpty()) { emit loginError(tr("Enter the code.")); return; }
    send(SignIn, TgApi::authSignIn(m_loginPhone, m_phoneCodeHash, c));
}

void TelegramSession::resendCode()
{
    if (m_loginPhone.isEmpty() || m_phoneCodeHash.isEmpty()) return;
    send(ResendCode, TgApi::authResendCode(m_loginPhone, m_phoneCodeHash));
}

void TelegramSession::backToPhoneEntry()
{
    m_codeNeeded = false;
    m_codeLength = 0;
    m_phoneCodeHash.clear();
    emit codeNeededChanged();
}

void TelegramSession::cancelPhoneLogin()
{
    if (m_loginPhone.isEmpty()) return;
    m_loginPhone.clear();
    m_phoneCodeHash.clear();
    m_codeNeeded = false;
    m_codeLength = 0;
    emit codeNeededChanged();
    if (m_state == LoggingIn && !m_passwordNeeded) exportToken();   // back to the QR code
}

void TelegramSession::onQrPoll()
{
    if (m_state != LoggingIn || m_passwordNeeded || m_moved) return;
    exportToken();
}

void TelegramSession::handleLoginStep(const TgQrLoginStep &step, bool fromMovedDc)
{
    switch (step.status) {
    case TgQrLoginStep::ShowToken:
        if (fromMovedDc) adoptMoved();
        if (step.token != m_qrToken) {
            m_qrToken = step.token;
            m_qrUrl = step.url();
            m_qrExpires = step.expires;
            emit qrChanged();
        }
        if (!m_qrPoll->isActive()) m_qrPoll->start();
        break;

    case TgQrLoginStep::Migrate: {
        if (m_moved) return;                    // already moving
        // The token has to be carried to the account's datacenter and imported there. The
        // old connection stays up while the new one is built: the token expires in
        // seconds and a handshake takes several.
        m_qrPoll->stop();
        m_movedDc = step.dcId;
        m_movedToken = step.token;
        emit log(QString::fromLatin1("login token migrates to dc%1").arg(step.dcId));
        m_moved = new MtprotoClient(this);
        m_moved->setInfo(m_info);
        m_moved->setProxy(m_proxy);
        connect(m_moved, SIGNAL(connected()), this, SLOT(onMovedConnected()));
        connect(m_moved, SIGNAL(disconnected(QString)), this, SLOT(onMovedDisconnected(QString)));
        connect(m_moved, SIGNAL(rpcResult(quint64,QByteArray)), this, SLOT(onRpcResult(quint64,QByteArray)));
        connect(m_moved, SIGNAL(rpcError(quint64,int,QString)), this, SLOT(onRpcError(quint64,int,QString)));
        connect(m_moved, SIGNAL(log(QString)), this, SIGNAL(log(QString)));
        m_moved->connectToDc(TelegramServers::hostFor(step.dcId), TelegramServers::DefaultPort);
        break;
    }

    case TgQrLoginStep::Success:
        if (fromMovedDc) adoptMoved();
        finishLogin();
        break;

    case TgQrLoginStep::PasswordNeeded:
        if (fromMovedDc) adoptMoved();
        m_qrPoll->stop();
        m_passwordNeeded = true;
        m_qrUrl.clear();
        emit qrChanged();
        send(GetPassword, TgApi::accountGetPassword());   // for the hint; the proof is computed later
        emit passwordNeededChanged();
        break;
    }
}

void TelegramSession::onMovedConnected()
{
    Request r;
    r.more = true;                              // marks "on the moved connection"
    quint64 id = m_moved->invoke(TgApi::importLoginToken(m_movedToken));
    r.kind = ImportToken;
    m_requests.insert(id, r);
}

void TelegramSession::onMovedDisconnected(const QString &reason)
{
    if (!m_moved) return;
    emit log(QLatin1String("moved connection lost: ") + reason);
    m_moved->deleteLater();
    m_moved = 0;
    if (m_state == LoggingIn) m_qrPoll->start();   // back to polling the old datacenter
}

void TelegramSession::adoptMoved()
{
    if (!m_moved) return;
    // The moved connection becomes the account's. The datacenter left behind holds an
    // authorisation that will never complete; ending it keeps Telegram's device list clean.
    MtprotoClient *old = m_client;
    old->invoke(TgApi::authLogOut());
    QTimer::singleShot(3000, old, SLOT(deleteLater()));
    disconnect(old, 0, this, 0);

    m_client = m_moved;
    m_moved = 0;
    disconnect(m_client, SIGNAL(connected()), this, SLOT(onMovedConnected()));
    disconnect(m_client, SIGNAL(disconnected(QString)), this, SLOT(onMovedDisconnected(QString)));
    connect(m_client, SIGNAL(connected()), this, SLOT(onConnected()));
    connect(m_client, SIGNAL(disconnected(QString)), this, SLOT(onDisconnected(QString)));
    connect(m_client, SIGNAL(updateReceived(TlObject)), this, SLOT(onUpdate(TlObject)));
    m_dcId = m_movedDc;
    m_authKey = m_client->authKey();
    saveSessionFile();
    emit log(QString::fromLatin1("now on dc%1").arg(m_dcId));
}

void TelegramSession::checkPassword(const QString &password)
{
    if (!m_passwordNeeded || m_srp) return;
    if (password.isEmpty()) { emit loginError(tr("Enter the password.")); return; }
    if (!m_srpParams.supported) {
        // The parameters carry a per-attempt value; fetch them fresh for the proof.
        m_srp = new SrpWorker(this);
        m_srp->password = password;
        send(GetPassword, TgApi::accountGetPassword());
        return;
    }
    m_srp = new SrpWorker(this);
    m_srp->password = password;
    m_srp->params = m_srpParams;
    m_srpParams = SrpParams();                  // single use
    connect(m_srp, SIGNAL(finished()), this, SLOT(onSrpDone()));
    m_srp->start();
}

void TelegramSession::onSrpDone()
{
    SrpWorker *w = m_srp;
    m_srp = 0;
    if (!w->error.isEmpty()) {
        emit loginError(w->error);
        w->deleteLater();
        return;
    }
    send(CheckPassword, TgApi::authCheckPassword(w->params.srpId, w->a, w->m1));
    w->deleteLater();
}

void TelegramSession::finishLogin()
{
    m_qrPoll->stop();
    m_signedIn = true;
    m_passwordNeeded = false;
    m_loginPhone.clear();
    m_phoneCodeHash.clear();
    m_codeNeeded = false;
    m_codeLength = 0;
    m_qrUrl.clear();
    m_qrToken.clear();
    m_updateState = TgUpdateState();
    saveSessionFile();
    emit qrChanged();
    emit signedIn();
    startSync();
}

void TelegramSession::logOut()
{
    m_loggingOut = true;
    if (m_client->isReady()) {
        send(LogOut, TgApi::authLogOut());
        QTimer::singleShot(8000, this, SLOT(onLogOutTimeout()));
    } else {
        forgetSession(tr("Signed out."));
    }
}

void TelegramSession::onLogOutTimeout()
{
    if (m_loggingOut) forgetSession(tr("Signed out."));
}

// -- sync ----------------------------------------------------------------------------------------------------

void TelegramSession::startSync()
{
    setState(Syncing);
    m_saveTimer->start();
    send(GetSelf, TgApi::usersGetSelf());
    if (m_updateState.isValid()) getDifference();
    else send(GetState, TgApi::updatesGetState());
    refreshDialogs();
    loadFolders();
    loadArchive();
    ensureDhConfig();
    if (m_online) send(UpdateStatus, TgApi::updateStatus(true));
}

void TelegramSession::getDifference()
{
    if (m_differencePending || !m_client->isReady()) return;
    m_differencePending = true;
    send(GetDifference, TgApi::updatesGetDifference(m_updateState));
}

void TelegramSession::refreshDialogs()
{
    if (m_dialogsLoading) return;
    requestDialogs(0, 0, TgPeer(), false, 0);
}

void TelegramSession::loadMoreDialogs()
{
    if (m_dialogsLoading || !m_dialogsHaveMore || m_dialogs.isEmpty()) return;
    // The next page starts after the oldest unpinned entry; the server orders by the date
    // of the last message and identifies the position by date, id and peer together.
    const TgDialog &last = m_dialogs.last();
    requestDialogs(last.topMessageDate, last.topMessageId, last.peer, true, 0);
}

void TelegramSession::requestDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, bool more, int folderId)
{
    if (!m_client->isReady()) return;
    Request r;
    r.more = more;
    r.folderId = folderId;
    if (folderId == 0) {
        m_dialogsLoading = true;
        send(GetDialogs, TgApi::getDialogs(offsetDate, offsetId, offsetPeer, DialogPageSize, 0), r);
    } else {
        send(GetArchive, TgApi::getDialogs(offsetDate, offsetId, offsetPeer, DialogPageSize, folderId), r);
    }
}

void TelegramSession::loadFolders()
{
    if (m_client->isReady()) send(GetFolders, TgApi::getDialogFilters());
}

void TelegramSession::loadArchive()
{
    requestDialogs(0, 0, TgPeer(), false, 1);
}

void TelegramSession::loadMoreArchive()
{
    if (!m_archiveHasMore || m_archived.isEmpty()) return;
    const TgDialog &last = m_archived.last();
    requestDialogs(last.topMessageDate, last.topMessageId, last.peer, true, 1);
}

void TelegramSession::applyArchive(const TgDialogPage &page, bool more)
{
    if (!more) m_archived = page.dialogs;
    else
        for (int i = 0; i < page.dialogs.size(); ++i)
            if (dialogIndexIn(m_archived, page.dialogs.at(i).peer) < 0) m_archived.append(page.dialogs.at(i));
    m_archiveHasMore = page.hasMore;
    m_archiveLoaded = true;
    // Remember which peers are archived - used to keep their messages out of the main list
    // and to suppress their notifications.
    for (int i = 0; i < page.dialogs.size(); ++i) {
        QString key = page.dialogs.at(i).peer.key();
        m_archivedPeers.insert(key);
        int mi = dialogIndex(page.dialogs.at(i).peer);
        if (mi >= 0) { m_dialogs.removeAt(mi); }
    }
    emit archiveChanged();
    emit dialogsChanged();
}

void TelegramSession::setArchived(const TgPeer &peer, bool archived)
{
    QString key = peer.key();
    if (archived == m_archivedPeers.contains(key)) {
        // still make sure it sits in the right list
    }
    if (archived) {
        m_archivedPeers.insert(key);
        int mi = dialogIndex(peer);
        if (mi >= 0) {
            TgDialog d = m_dialogs.takeAt(mi);
            d.archived = true;
            if (dialogIndexIn(m_archived, peer) < 0) m_archived.append(d);
            emit dialogsChanged();
            emit archiveChanged();
        }
    } else {
        m_archivedPeers.remove(key);
        int ai = dialogIndexIn(m_archived, peer);
        if (ai >= 0) {
            TgDialog d = m_archived.takeAt(ai);
            d.archived = false;
            if (dialogIndex(peer) < 0) m_dialogs.append(d);
            sortDialogs();
            emit dialogsChanged();
            emit archiveChanged();
        }
    }
}

void TelegramSession::applyDialogs(const TgDialogPage &page, bool more)
{
    if (!more) {
        // Keep entries this page does not mention - added locally, or loaded on a later
        // page: the refresh only sees the first page.
        QList<TgDialog> merged = page.dialogs;
        for (int i = 0; i < m_dialogs.size(); ++i)
            if (dialogIndexIn(merged, m_dialogs.at(i).peer) < 0) merged.append(m_dialogs.at(i));
        m_dialogs = merged;
    } else {
        for (int i = 0; i < page.dialogs.size(); ++i)
            if (dialogIndex(page.dialogs.at(i).peer) < 0) m_dialogs.append(page.dialogs.at(i));
    }
    m_dialogsHaveMore = page.hasMore;
    sortDialogs();
    emit dialogsChanged();
}

int TelegramSession::dialogIndexIn(const QList<TgDialog> &list, const TgPeer &peer)
{
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).peer == peer) return i;
    return -1;
}

int TelegramSession::dialogIndex(const TgPeer &peer) const
{
    return dialogIndexIn(m_dialogs, peer);
}

TgDialog TelegramSession::dialog(const TgPeer &peer) const
{
    int i = dialogIndex(peer);
    return i >= 0 ? m_dialogs.at(i) : TgDialog();
}

namespace
{
    bool dialogLater(const TgDialog &a, const TgDialog &b)
    {
        if (a.pinned != b.pinned) return a.pinned;
        return a.topMessageDate > b.topMessageDate;
    }
}

void TelegramSession::sortDialogs()
{
    qStableSort(m_dialogs.begin(), m_dialogs.end(), dialogLater);
}

QString TelegramSession::selfName() const
{
    return m_selfId ? m_peers.title(TgPeer(TgPeer::User, m_selfId)) : QString();
}

void TelegramSession::ensureDialog(const TgPeer &peer)
{
    if (dialogIndex(peer) >= 0) return;
    TgDialog d;
    d.peer = m_peers.withHash(peer);
    m_dialogs.append(d);
    sortDialogs();
    emit dialogsChanged();
}

// -- actions ---------------------------------------------------------------------------------------------------

void TelegramSession::loadHistory(const TgPeer &peer, int offsetId, int count)
{
    Request r;
    r.peer = m_peers.withHash(peer);
    r.offsetId = offsetId;
    r.more = count;
    send(GetHistory, TgApi::getHistory(r.peer, offsetId, count), r);
}

qint64 TelegramSession::sendText(const TgPeer &peer, const QString &text, int replyToId)
{
    Request r;
    r.peer = m_peers.withHash(peer);
    r.randomId = qint64(Crypto::randomUInt64());
    r.query = text;
    send(SendMessage, TgApi::sendMessage(r.peer, text, r.randomId, replyToId), r);
    return r.randomId;
}

void TelegramSession::markRead(const TgPeer &peer, int maxId)
{
    if (!m_client->isReady()) return;
    Request r;
    r.peer = m_peers.withHash(peer);
    r.offsetId = maxId;
    send(ReadHistory, TgApi::readHistory(r.peer, maxId), r);
    int i = dialogIndex(peer);
    if (i >= 0 && (m_dialogs[i].unreadCount != 0 || maxId > m_dialogs[i].readInboxMaxId)) {
        m_dialogs[i].unreadCount = 0;
        m_dialogs[i].readInboxMaxId = qMax(m_dialogs[i].readInboxMaxId, maxId);
        emit dialogChanged(peer);
    }
}

void TelegramSession::setTyping(const TgPeer &peer, bool typing)
{
    if (!m_client->isReady()) return;
    Request r;
    r.peer = m_peers.withHash(peer);
    send(SetTyping, TgApi::setTyping(r.peer, typing), r);
}

void TelegramSession::setOnline(bool online)
{
    m_online = online;
    if (m_state == Online || m_state == Syncing) send(UpdateStatus, TgApi::updateStatus(online));
}

void TelegramSession::resolve(const QString &query)
{
    QString q = query.trimmed();
    if (q.isEmpty()) { emit resolveFailed(tr("Enter a username, a phone number or a name.")); return; }
    if (!m_client->isReady()) { emit resolveFailed(tr("Not connected.")); return; }
    Request r;
    r.query = q;
    if (TgApi::looksLikePhone(q)) {
        send(ResolvePhone, TgApi::resolvePhone(TgApi::normalisePhone(q)), r);
    } else if (q.startsWith(QLatin1Char('@')) || q.contains(QLatin1String("t.me/")) || !q.contains(QLatin1Char(' '))) {
        send(ResolveUsername, TgApi::resolveUsername(TgApi::normaliseUsername(q)), r);
    } else {
        send(ContactsSearch, TgApi::contactsSearch(q, 5), r);
    }
}

void TelegramSession::deleteHistory(const TgPeer &peer)
{
    Request r;
    r.peer = m_peers.withHash(peer);
    send(DeleteHistory, TgApi::deleteHistory(r.peer, true), r);
}

void TelegramSession::removeDialogLocal(const TgPeer &peer)
{
    int mi = dialogIndex(peer);
    if (mi >= 0) m_dialogs.removeAt(mi);
    int ai = dialogIndexIn(m_archived, peer);
    if (ai >= 0) m_archived.removeAt(ai);
    m_archivedPeers.remove(peer.key());
    emit dialogsChanged();
    emit archiveChanged();
    emit dialogChanged(peer);
}

void TelegramSession::deleteChat(const TgPeer &peer, bool forEveryone)
{
    TgPeer p = m_peers.withHash(peer);
    Request r;
    r.peer = p;
    r.revoke = forEveryone;
    QByteArray body;
    if (p.kind == TgPeer::Channel) body = TgApi::leaveChannel(p);
    else if (p.kind == TgPeer::Chat) body = TgApi::deleteChatUser(p.id, forEveryone);
    else body = TgApi::deleteHistory(p, false, forEveryone);
    send(DeleteChat, body, r);
    removeDialogLocal(p);          // optimistic: it is gone from the UI at once
}

void TelegramSession::archiveChat(const TgPeer &peer, bool archived)
{
    TgPeer p = m_peers.withHash(peer);
    setArchived(p, archived);      // optimistic local move between the two lists
    Request r;
    r.peer = p;
    send(ArchivePeer, TgApi::editPeerFolders(p, archived ? 1 : 0), r);
}

void TelegramSession::moveToFolder(const TgPeer &peer, int filterId, bool remove)
{
    TgPeer p = m_peers.withHash(peer);
    int fi = -1;
    for (int i = 0; i < m_folders.size(); ++i) if (m_folders.at(i).id == filterId) { fi = i; break; }
    if (fi < 0) { emit notice(tr("That folder no longer exists.")); return; }
    QByteArray filter = remove ? buildFolderFilter(m_folders.at(fi), TgPeer(), p)
                               : buildFolderFilter(m_folders.at(fi), p, TgPeer());
    // Update the cached folder now, so the view reflects it and - crucially - a second move
    // before the server round-trip rebuilds from fresh data instead of clobbering this one.
    const QString key = p.key();
    if (remove) m_folders[fi].include.removeAll(key);
    else { m_folders[fi].exclude.removeAll(key); if (!m_folders[fi].include.contains(key)) m_folders[fi].include.append(key); }
    emit foldersChanged();
    Request r;
    r.peer = p;
    send(MoveFolder, TgApi::updateDialogFilter(filter, filterId), r);
}

void TelegramSession::setChatFolder(const TgPeer &peer, int destFilterId)
{
    // "Move" semantics: the chat ends up in exactly the destination folder. Remove it from every
    // other custom folder it currently sits in, then add it to the destination (destFilterId < 0
    // means "All chats" - no custom folder). Each folder is its own dialogFilter, sent separately.
    TgPeer p = m_peers.withHash(peer);
    const QString key = p.key();
    for (int i = 0; i < m_folders.size(); ++i) {
        if (m_folders.at(i).id == destFilterId) continue;
        if (!m_folders.at(i).include.contains(key)) continue;
        QByteArray filter = buildFolderFilter(m_folders.at(i), TgPeer(), p);
        m_folders[i].include.removeAll(key);          // update cache before the round-trip
        Request r; r.peer = p;
        send(MoveFolder, TgApi::updateDialogFilter(filter, m_folders.at(i).id), r);
    }
    if (destFilterId >= 0) {
        int fi = -1;
        for (int i = 0; i < m_folders.size(); ++i) if (m_folders.at(i).id == destFilterId) { fi = i; break; }
        if (fi < 0) { emit notice(tr("That folder no longer exists.")); emit foldersChanged(); return; }
        if (!m_folders.at(fi).include.contains(key)) {
            QByteArray filter = buildFolderFilter(m_folders.at(fi), p, TgPeer());
            m_folders[fi].exclude.removeAll(key);
            m_folders[fi].include.append(key);
            Request r; r.peer = p;
            send(MoveFolder, TgApi::updateDialogFilter(filter, m_folders.at(fi).id), r);
        }
    }
    emit foldersChanged();
}

QByteArray TelegramSession::buildFolderFilter(const TgFolder &f, const TgPeer &addPeer, const TgPeer &removePeer) const
{
    int flags = 0;
    if (f.contacts) flags |= 1 << 0;
    if (f.nonContacts) flags |= 1 << 1;
    if (f.groups) flags |= 1 << 2;
    if (f.broadcasts) flags |= 1 << 3;
    if (f.bots) flags |= 1 << 4;
    if (f.excludeMuted) flags |= 1 << 11;
    if (f.excludeRead) flags |= 1 << 12;
    if (f.excludeArchived) flags |= 1 << 13;
    if (!f.emoticon.isEmpty()) flags |= 1 << 25;
    if (f.hasColor) flags |= 1 << 27;

    QList<QString> inc = f.include, exc = f.exclude;
    const QString addKey = addPeer.isNull() ? QString() : addPeer.key();
    const QString remKey = removePeer.isNull() ? QString() : removePeer.key();
    if (!remKey.isEmpty()) inc.removeAll(remKey);
    if (!addKey.isEmpty()) { exc.removeAll(addKey); if (!inc.contains(addKey)) inc.append(addKey); }

    TlWriter w(256);
    w.writeConstructor(Tl::DialogFilter).writeInt(flags).writeInt(f.id);
    w.writeConstructor(Tl::TextWithEntities).writeString(f.title).writeConstructor(Tl::Vector).writeInt(0);
    if (!f.emoticon.isEmpty()) w.writeString(f.emoticon);
    if (f.hasColor) w.writeInt(f.color);
    const QList<QString> *lists[3] = { &f.pinned, &inc, &exc };
    for (int L = 0; L < 3; ++L) {
        const QList<QString> &keys = *lists[L];
        w.writeConstructor(Tl::Vector).writeInt(keys.size());
        for (int i = 0; i < keys.size(); ++i) w.writeRaw(TgApi::inputPeer(m_peers.withHash(TgPeer::fromKey(keys.at(i)))));
    }
    return w.toByteArray();
}

void TelegramSession::applyUpdatesResult(const TlObject &o)
{
    m_peers.absorb(o);
    QVariantList list = o.vec("updates");
    for (int i = 0; i < list.size(); ++i) applyUpdate(TlSchema::toObject(list.at(i)));
}

void TelegramSession::deleteMessages(const TgPeer &peer, const QList<int> &ids, bool revoke)
{
    Request r;
    r.peer = m_peers.withHash(peer);
    send(DeleteMessages, TgApi::deleteMessages(r.peer, ids, revoke), r);
}

void TelegramSession::setMuted(const TgPeer &peer, bool muted)
{
    Request r;
    r.peer = m_peers.withHash(peer);
    r.more = muted;
    send(UpdateNotifySettings, TgApi::updateNotifySettings(r.peer, muted), r);
}

// -- results -------------------------------------------------------------------------------------------------

void TelegramSession::onRpcResult(quint64 requestId, const QByteArray &result)
{
    if (!m_requests.contains(requestId)) return;
    Request req = m_requests.take(requestId);
    try {
        TlReader r(result);
        switch (req.kind) {
        case ExportToken:
        case ImportToken: {
            TlObject o = TlSchema::readObject(r);
            handleLoginStep(TgApi::readLoginToken(o), req.kind == ImportToken && req.more);
            break;
        }
        case GetPassword: {
            SrpParams p = Srp::readPasswordParams(result);
            m_passwordHint = p.hint;
            if (!p.hasPassword) { emit loginError(tr("This account has no password set.")); break; }
            if (!p.supported) { emit loginError(tr("Unsupported password method.")); break; }
            m_srpParams = p;
            emit passwordNeededChanged();
            if (m_srp && !m_srp->isRunning() && m_srp->a.isEmpty()) {
                // checkPassword() was waiting for fresh parameters.
                m_srp->params = p;
                m_srpParams = SrpParams();
                connect(m_srp, SIGNAL(finished()), this, SLOT(onSrpDone()));
                m_srp->start();
            }
            break;
        }
        case CheckPassword:
            r.expect(Tl::AuthAuthorization, "auth.authorization");
            finishLogin();
            break;
        case SendCode:
        case ResendCode: {
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() == Tl::AuthSentCodeSuccess) { finishLogin(); break; }   // already authorised
            m_phoneCodeHash = o.str("phone_code_hash");
            m_codeLength = o.has("type") ? o.obj("type").intOr("length") : 0;
            m_codeNeeded = true;
            emit codeNeededChanged();
            break;
        }
        case SignIn: {
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() == Tl::AuthAuthorizationSignUpRequired) {
                emit loginError(tr("No Telegram account uses this number. Sign-up isn't supported here."));
                break;
            }
            finishLogin();      // auth.authorization
            break;
        }
        case LogOut:
            forgetSession(tr("Signed out."));
            break;
        case GetSelf: {
            r.expect(Tl::Vector, "vector");
            int n = r.readInt();
            if (n >= 1) {
                TlObject u = TlSchema::readObject(r);
                m_peers.absorbUser(u);
                m_selfId = u.longOr("id");
                m_stateDirty = true;
                emit selfChanged();
            }
            break;
        }
        case GetState: {
            TlObject o = TlSchema::readObject(r);
            m_updateState = TgApi::readState(o);
            m_stateDirty = true;
            emit log(QString::fromLatin1("update state pts=%1 date=%2").arg(m_updateState.pts).arg(m_updateState.date));
            break;
        }
        case GetDifference: {
            m_differencePending = false;
            TlObject diff = TlSchema::readObject(r);
            if (diff.ctor() == Tl::UpdatesDifferenceEmpty) {
                m_updateState.date = diff.intOr("date", m_updateState.date);
                m_updateState.seq = diff.intOr("seq", m_updateState.seq);
                m_stateDirty = true;
                break;
            }
            if (diff.ctor() == Tl::UpdatesDifferenceTooLong) {
                // Too far behind to catch up incrementally: take the new position and
                // reload the list from scratch.
                m_updateState.pts = diff.intOr("pts", m_updateState.pts);
                m_stateDirty = true;
                refreshDialogs();
                break;
            }
            m_peers.absorb(diff);
            QVariantList msgs = diff.vec("new_messages");
            for (int i = 0; i < msgs.size(); ++i) {
                TlObject o = TlSchema::toObject(msgs.at(i));
                TgMessage m = TgApi::readMessage(o);
                if (m.service && o.has("action")) m.note = TgApi::describeAction(o.obj("action"), m_peers);
                applyMessage(m, true);
            }
            QVariantList others = diff.vec("other_updates");
            for (int i = 0; i < others.size(); ++i) applyUpdate(TlSchema::toObject(others.at(i)));
            QVariantList encs = diff.vec("new_encrypted_messages");
            for (int i = 0; i < encs.size(); ++i) handleEncryptedMessage(TlSchema::toObject(encs.at(i)), unixNow());
            TlObject st = diff.has("state") ? diff.obj("state") : diff.obj("intermediate_state");
            if (!st.isNull()) {
                m_updateState = TgApi::readState(st);
                m_stateDirty = true;
            }
            if (diff.ctor() == Tl::UpdatesDifferenceSlice) getDifference();
            break;
        }
        case GetDialogs: {
            m_dialogsLoading = false;
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() != Tl::MessagesDialogsNotModified) applyDialogs(TgApi::readDialogs(o, m_peers, false), req.more);
            if (m_state == Syncing) setState(Online);
            break;
        }
        case GetArchive: {
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() != Tl::MessagesDialogsNotModified) applyArchive(TgApi::readDialogs(o, m_peers, true), req.more);
            break;
        }
        case GetFolders: {
            TlObject o = TlSchema::readObject(r);
            m_peers.absorb(o);
            m_folders = TgApi::readFolders(o, m_selfId);
            emit foldersChanged();
            break;
        }
        case GetDhConfig: {
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() == Tl::MessagesDhConfig) {
                m_dhG = o.intOr("g");
                m_dhP = o.bytes("p");
                m_dhVersion = o.intOr("version");
                // Validate the prime and generator once (the slow safe-prime check); the
                // config is the same for every secret chat, so this is paid once.
                QString note;
                try {
                    DhValidation::validatePrime(m_dhG, BigInt::fromBytesBE(m_dhP), &note);
                    m_dhReady = true;
                    emit log(QLatin1String("secret chats: ") + note);
                } catch (const TlException &e) {
                    emit log(QLatin1String("secret chats: DH config rejected: ") + e.message());
                    m_dhReady = false;
                }
            }
            if (m_dhReady) {
                QList<TgPeer> reqs = m_secretRequestQueue; m_secretRequestQueue.clear();
                for (int i = 0; i < reqs.size(); ++i) requestSecretChat(reqs.at(i));
                QList<int> accs = m_secretAcceptQueue; m_secretAcceptQueue.clear();
                for (int i = 0; i < accs.size(); ++i) acceptSecretChat(accs.at(i));
            }
            break;
        }
        case RequestEncryption: {
            TlObject o = TlSchema::readObject(r);
            SecretChat *chat = req.secret;
            emit log(QString::fromLatin1("secret: requestEncryption result ctor 0x%1").arg(o.ctor(), 0, 16));
            if (!chat) break;
            if (o.ctor() == Tl::EncryptedChatWaiting || o.ctor() == Tl::EncryptedChat || o.ctor() == Tl::EncryptedChatRequested) {
                int id = o.intOr("id");
                chat->setAccess(id, o.longOr("access_hash"));
                m_secretChats.insert(id, chat);
                if (o.ctor() == Tl::EncryptedChat) handleEncryptedChat(o);   // instantly accepted (rare)
                saveSecrets();
                emit secretChatsChanged();
            } else {
                delete chat;   // discarded
            }
            break;
        }
        case AcceptEncryption: {
            TlObject o = TlSchema::readObject(r);
            handleEncryptedChat(o);
            break;
        }
        case SendEncrypted: {
            TlObject o = TlSchema::readObject(r);
            int date = o.intOr("date", unixNow());
            saveSecrets();
            emit secretMessageSent(req.secretChatId, req.randomId, date);
            break;
        }
        case DiscardEncryption:
            break;
        case ArchivePeer:
            applyUpdatesResult(TlSchema::readObject(r));
            break;
        case MoveFolder:
            TlSchema::readObject(r);
            loadFolders();          // reconcile the cached filters with the server's truth
            break;
        case DeleteChat: {
            TlObject o = TlSchema::readObject(r);
            if (o.has("pts")) {                    // messages.affectedHistory (deleting a 1:1 chat)
                checkPts(o);
                if (o.intOr("offset") > 0) send(DeleteChat, TgApi::deleteHistory(req.peer, false, req.revoke), req);
            } else {
                applyUpdatesResult(o);             // Updates (leaving a group or channel)
            }
            break;
        }
        case GetHistory: {
            TlObject o = TlSchema::readObject(r);
            TgHistoryPage page = TgApi::readHistory(o, m_peers);
            for (int i = 0; i < page.messages.size(); ++i) fillSender(page.messages[i]);
            bool more = page.messages.size() >= int(req.more);
            if (o.ctor() == Tl::MessagesMessages) more = false;
            emit historyLoaded(req.peer, page.messages, req.offsetId, more);
            break;
        }
        case SendMessage: {
            TlObject o = TlSchema::readObject(r);
            int id = TgApi::sentMessageId(o);
            int date = o.intOr("date", unixNow());
            if (o.ctor() == Tl::UpdateShortSentMessage) {
                checkPts(o);
            } else {
                // A full Updates object (e.g. for a channel post): it also carries the
                // message itself, which applyUpdate would echo; only the pts matters.
                QVariantList list = o.vec("updates");
                for (int i = 0; i < list.size(); ++i) {
                    TlObject u = TlSchema::toObject(list.at(i));
                    if (u.ctor() == Tl::UpdateNewMessage) checkPts(u);
                }
            }
            TgMessage sent;
            sent.id = id;
            sent.date = date;
            sent.out = true;
            sent.peer = req.peer;
            sent.fromId = m_selfId;
            sent.text = req.query;
            int di = dialogIndex(req.peer);
            if (di >= 0) {
                m_dialogs[di].topMessageId = qMax(m_dialogs[di].topMessageId, id);
                m_dialogs[di].topMessageDate = qMax(m_dialogs[di].topMessageDate, date);
                m_dialogs[di].lastOut = true;
                m_dialogs[di].lastFromId = m_selfId;
                m_dialogs[di].lastText = sent.text;
                sortDialogs();
            }
            emit messageSent(req.peer, req.randomId, sent);
            if (di >= 0) emit dialogChanged(req.peer);
            break;
        }
        case SendMedia: {
            TlObject o = TlSchema::readObject(r);
            m_peers.absorb(o);
            QVariantList list = o.vec("updates");
            for (int i = 0; i < list.size(); ++i) {
                TlObject u = TlSchema::toObject(list.at(i));
                if (u.ctor() == Tl::UpdateNewMessage) checkPts(u);
            }
            QList<TgMessage> msgs = TgApi::messagesIn(o);
            TgMessage sent;
            if (!msgs.isEmpty()) sent = msgs.first();
            else { sent.id = TgApi::sentMessageId(o); sent.date = unixNow(); }
            sent.out = true;
            sent.peer = req.peer;
            sent.fromId = m_selfId;
            int di = dialogIndex(req.peer);
            if (di >= 0) {
                m_dialogs[di].topMessageId = qMax(m_dialogs[di].topMessageId, sent.id);
                m_dialogs[di].topMessageDate = qMax(m_dialogs[di].topMessageDate, sent.date);
                m_dialogs[di].lastOut = true;
                m_dialogs[di].lastFromId = m_selfId;
                m_dialogs[di].lastText = sent.text.isEmpty() ? sent.note : sent.text;
                sortDialogs();
            }
            m_uploads.remove(req.randomId);
            emit messageSent(req.peer, req.randomId, sent);
            if (di >= 0) emit dialogChanged(req.peer);
            break;
        }
        case SaveFilePart: {
            if (!m_uploads.contains(req.randomId)) break;
            if (!r.readBool()) { finishUpload(req.randomId, tr("the server rejected a part of the file")); break; }
            Upload &u = m_uploads[req.randomId];
            emit uploadProgress(u.randomId, qMin(u.size, qint64(u.nextPart) * PartSize), u.size);
            if (u.nextPart >= u.parts) {
                u.file->close();
                Request rq;
                rq.peer = u.peer;
                rq.randomId = u.randomId;
                if (u.voice)
                    send(SendMedia, TgApi::sendUploadedVoice(u.peer, u.fileId, u.parts, u.big, u.fileName,
                                                             u.durationSec, u.waveform, u.randomId), rq);
                else
                    send(SendMedia, TgApi::sendUploadedMedia(u.peer, u.fileId, u.parts, u.big, u.fileName, u.asPhoto,
                                                             TgApi::mimeTypeFor(u.fileName), u.caption, u.randomId), rq);
            } else {
                sendNextPart(u);
            }
            break;
        }
        case GetFile: {
            if (!m_downloads.contains(req.offsetId)) break;
            Download &d = m_downloads[req.offsetId];
            TlObject o = TlSchema::readObject(r);
            if (o.ctor() == Tl::UploadFileCdnRedirect) { finishDownload(d.jobId, tr("the file is served from a CDN, which is not supported")); break; }
            if (o.ctor() != Tl::UploadFile) { finishDownload(d.jobId, tr("unexpected reply")); break; }
            QByteArray bytes = o.bytes("bytes");
            if (!bytes.isEmpty()) {
                if (d.file->write(bytes) != bytes.size()) { finishDownload(d.jobId, tr("could not write the file")); break; }
                d.offset += bytes.size();
                emit downloadProgress(d.jobId, d.offset, d.total);
            }
            // A short read means the end, whatever the declared size said.
            if (bytes.size() < ChunkSize || (d.total > 0 && d.offset >= d.total)) finishDownload(d.jobId, QString());
            else requestChunk(d, clientForDc(d.dcId));
            break;
        }
        case ExportAuthorization: {
            TlObject o = TlSchema::readObject(r);
            int dc = req.offsetId;
            if (!m_dcLinks.contains(dc)) break;
            Request rq;
            rq.offsetId = dc;
            sendOn(m_dcLinks[dc].client, ImportAuthorization, TgApi::importAuthorization(o.longOr("id"), o.bytes("bytes")), rq);
            break;
        }
        case ImportAuthorization:
            dcAuthorized(req.offsetId);
            break;
        case ResolveUsername:
        case ResolvePhone: {
            TlObject o = TlSchema::readObject(r);
            m_peers.absorb(o);
            TgPeer p = m_peers.withHash(TgApi::readPeer(o.obj("peer")));
            if (p.isNull()) { emit resolveFailed(tr("Nobody found.")); break; }
            ensureDialog(p);
            emit peerResolved(p);
            break;
        }
        case ContactsSearch: {
            TlObject o = TlSchema::readObject(r);
            m_peers.absorb(o);
            QVariantList results = o.vec("my_results") + o.vec("results");
            if (results.isEmpty()) { emit resolveFailed(tr("Nobody found.")); break; }
            TgPeer p = m_peers.withHash(TgApi::readPeer(TlSchema::toObject(results.first())));
            ensureDialog(p);
            emit peerResolved(p);
            break;
        }
        case DeleteHistory: {
            TlObject o = TlSchema::readObject(r);
            if (o.intOr("offset") > 0) deleteHistory(req.peer);   // more than one call covered
            int di = dialogIndex(req.peer);
            if (di >= 0) { m_dialogs[di].lastText.clear(); m_dialogs[di].unreadCount = 0; emit dialogChanged(req.peer); }
            break;
        }
        case UpdateNotifySettings: {
            int di = dialogIndex(req.peer);
            if (di >= 0) { m_dialogs[di].mutedUntil = req.more ? 0x7fffffff : 0; emit dialogChanged(req.peer); }
            break;
        }
        case GetUser: {
            r.expect(Tl::Vector, "vector");
            int n = r.readInt();
            for (int i = 0; i < n; ++i) m_peers.absorbUser(TlSchema::readObject(r));
            emit peerChanged(req.peer);
            int di = dialogIndex(req.peer);
            if (di >= 0) emit dialogChanged(req.peer);
            break;
        }
        case ReadHistory:
        case SetTyping:
        case UpdateStatus:
        case DeleteMessages:
            break;
        }
    } catch (const TlException &e) {
        emit log(QString::fromLatin1("result of request kind %1 unreadable: %2").arg(int(req.kind)).arg(e.message()));
        if (req.kind == GetDialogs) { m_dialogsLoading = false; if (m_state == Syncing) setState(Online); }
        if (req.kind == GetDifference) m_differencePending = false;
        if (req.kind == GetHistory) emit historyFailed(req.peer, e.message());
        if (req.kind == SendMessage) emit messageFailed(req.peer, req.randomId, e.message());
    }
}

void TelegramSession::onRpcError(quint64 requestId, int code, const QString &type)
{
    if (!m_requests.contains(requestId)) return;
    Request req = m_requests.take(requestId);
    emit log(QString::fromLatin1("rpc error %1 %2 (request kind %3)").arg(code).arg(type).arg(int(req.kind)));

    if (isAuthGone(type) && req.kind != LogOut && req.kind != ExportToken && req.kind != ImportToken
        && req.kind != GetFile && req.kind != ImportAuthorization) {
        forgetSession(tr("The session was ended (%1). Please sign in again.").arg(type));
        return;
    }
    if (type.contains(QLatin1String("_MIGRATE_")) && req.kind != ExportToken && req.kind != ImportToken && req.kind != GetFile) {
        handleMigrate(type);
        return;
    }

    switch (req.kind) {
    case ExportToken:
    case ImportToken:
        if (type.contains(QLatin1String("SESSION_PASSWORD_NEEDED"))) {
            TgQrLoginStep step;
            step.status = TgQrLoginStep::PasswordNeeded;
            handleLoginStep(step, req.kind == ImportToken && req.more);
        } else if (type.contains(QLatin1String("AUTH_TOKEN_EXPIRED")) || type.contains(QLatin1String("AUTH_TOKEN_INVALID"))) {
            if (req.kind == ImportToken && m_moved) {
                // The token died while the new datacenter was being reached. Ask the old
                // one for a fresh token; the next migrate answer will be imported.
                m_moved->deleteLater();
                m_moved = 0;
                exportToken();
            } else if (m_state == LoggingIn && !m_qrPoll->isActive()) {
                m_qrPoll->start();
            }
        } else if (type.startsWith(QLatin1String("NETWORK"))) {
            // reported through disconnected()
        } else {
            emit loginError(type);
            if (m_state == LoggingIn && !m_qrPoll->isActive() && !m_passwordNeeded) m_qrPoll->start();
        }
        break;
    case GetPassword:
        if (m_srp) { m_srp->deleteLater(); m_srp = 0; }
        emit loginError(type);
        break;
    case CheckPassword:
        if (type.contains(QLatin1String("PASSWORD_HASH_INVALID"))) emit loginError(tr("Wrong password."));
        else if (type.startsWith(QLatin1String("FLOOD_WAIT_"))) emit loginError(tr("Too many attempts. Wait %1 seconds.").arg(type.mid(11)));
        else emit loginError(type);
        m_srpParams = SrpParams();
        send(GetPassword, TgApi::accountGetPassword());
        break;
    case SendCode:
    case ResendCode:
        // PHONE_MIGRATE_* is handled above (reconnects and resends). Everything else is fatal
        // for this attempt: report it and leave the user on the phone-number field.
        if (type.startsWith(QLatin1String("FLOOD_WAIT_"))) emit loginError(tr("Too many attempts. Wait %1 seconds.").arg(type.mid(11)));
        else if (type.contains(QLatin1String("PHONE_NUMBER_INVALID"))) emit loginError(tr("That phone number is not valid."));
        else if (type.contains(QLatin1String("PHONE_NUMBER_BANNED"))) emit loginError(tr("That phone number is banned from Telegram."));
        else if (type.contains(QLatin1String("PHONE_NUMBER_FLOOD"))) emit loginError(tr("Too many codes requested for this number. Try again later."));
        else if (type.contains(QLatin1String("PHONE_PASSWORD_FLOOD"))) emit loginError(tr("Too many attempts. Try again later."));
        else if (type.contains(QLatin1String("API_ID_INVALID"))) emit loginError(tr("This build's Telegram API key was rejected."));
        else emit loginError(type);
        break;
    case SignIn:
        if (type.contains(QLatin1String("SESSION_PASSWORD_NEEDED"))) {
            // The account has two-step verification: hand over to the password step (SRP).
            m_codeNeeded = false;
            emit codeNeededChanged();
            m_passwordNeeded = true;
            send(GetPassword, TgApi::accountGetPassword());
            emit passwordNeededChanged();
        } else if (type.contains(QLatin1String("PHONE_CODE_INVALID"))) {
            emit loginError(tr("Wrong code. Check it and try again."));
        } else if (type.contains(QLatin1String("PHONE_CODE_EXPIRED"))) {
            emit loginError(tr("The code expired. Request a new one."));
        } else if (type.contains(QLatin1String("PHONE_CODE_EMPTY"))) {
            emit loginError(tr("Enter the code."));
        } else if (type.contains(QLatin1String("PHONE_NUMBER_UNOCCUPIED"))) {
            emit loginError(tr("No Telegram account uses this number. Sign-up isn't supported here."));
        } else if (type.startsWith(QLatin1String("FLOOD_WAIT_"))) {
            emit loginError(tr("Too many attempts. Wait %1 seconds.").arg(type.mid(11)));
        } else {
            emit loginError(type);
        }
        break;
    case LogOut:
        forgetSession(tr("Signed out."));
        break;
    case GetDialogs:
        m_dialogsLoading = false;
        if (m_state == Syncing) setState(Online);
        emit notice(tr("Could not load the chat list: %1").arg(type));
        break;
    case GetDifference:
        m_differencePending = false;
        break;
    case GetHistory:
        emit historyFailed(req.peer, type);
        break;
    case SendMessage:
        emit messageFailed(req.peer, req.randomId, type);
        break;
    case SendMedia:
    case SaveFilePart:
        finishUpload(req.randomId, type);
        break;
    case GetFile: {
        if (!m_downloads.contains(req.offsetId)) break;
        Download &d = m_downloads[req.offsetId];
        if (type.contains(QLatin1String("FILE_MIGRATE_")) && d.migrations < 2) {
            // The file lives on another datacenter: carry on there.
            ++d.migrations;
            d.dcId = type.mid(type.lastIndexOf(QLatin1Char('_')) + 1).toInt();
            d.active = false;
            pumpDownloads();
        } else if (type.contains(QLatin1String("FILE_REFERENCE"))) {
            finishDownload(d.jobId, QLatin1String("FILE_REFERENCE_EXPIRED"));
        } else {
            finishDownload(d.jobId, type);
        }
        break;
    }
    case ExportAuthorization:
    case ImportAuthorization:
        failTransfersOnDc(req.offsetId, type);
        if (m_dcLinks.contains(req.offsetId)) { m_dcLinks[req.offsetId].client->deleteLater(); m_dcLinks.remove(req.offsetId); }
        break;
    case ResolveUsername:
    case ResolvePhone:
    case ContactsSearch:
        if (type.contains(QLatin1String("USERNAME_NOT_OCCUPIED")) || type.contains(QLatin1String("PHONE_NOT_OCCUPIED")) || type.contains(QLatin1String("USERNAME_INVALID")))
            emit resolveFailed(tr("Nobody found."));
        else emit resolveFailed(type);
        break;
    case GetArchive:
    case GetFolders:
    case GetDhConfig:
        m_secretRequestQueue.clear();
        m_secretAcceptQueue.clear();
        break;
    case RequestEncryption:
        if (req.secret) delete req.secret;
        emit notice(tr("Could not start the secret chat: %1").arg(type));
        break;
    case AcceptEncryption:
        emit notice(tr("Could not accept the secret chat: %1").arg(type));
        break;
    case SendEncrypted:
        emit secretMessageFailed(req.secretChatId, req.randomId, type);
        break;
    case DiscardEncryption:
    case GetState:
    case GetSelf:
    case ReadHistory:
    case SetTyping:
    case UpdateStatus:
    case DeleteHistory:
    case DeleteMessages:
    case UpdateNotifySettings:
    case GetUser:
    case ArchivePeer:
    case DeleteChat:
        break;
    case MoveFolder:
        // Telegram won't let a folder with no category filters end up with an empty chat list,
        // so the very last chat can't be removed from such a folder. Explain rather than dump the code.
        if (type == QLatin1String("FILTER_INCLUDE_EMPTY"))
            emit notice(tr("That folder must keep at least one chat, so the chat stays in it too."));
        else
            emit notice(tr("Could not change the folder: %1").arg(type));
        loadFolders();      // re-sync the cached filters with the server after a rejected change
        break;
    }
}

// -- updates ------------------------------------------------------------------------------------------------

void TelegramSession::onUpdate(const TlObject &u)
{
    try {
        switch (u.ctor()) {
        case Tl::UpdateShortMessage:
        case Tl::UpdateShortChatMessage: {
            if (!checkPts(u)) return;
            TgMessage m = TgApi::readShortMessage(u, m_selfId);
            if (m.peer.kind == TgPeer::User && !m_peers.contains(m.peer)) {
                // A stranger: the short form carries no user object; the list refresh
                // brings it.
                refreshDialogs();
            }
            applyMessage(m, false);
            break;
        }
        case Tl::UpdateShort:
            m_updateState.date = qMax(m_updateState.date, u.intOr("date"));
            applyUpdate(u.obj("update"));
            break;
        case Tl::Updates:
        case Tl::UpdatesCombined: {
            m_peers.absorb(u);
            m_updateState.date = qMax(m_updateState.date, u.intOr("date"));
            QVariantList list = u.vec("updates");
            for (int i = 0; i < list.size(); ++i) applyUpdate(TlSchema::toObject(list.at(i)));
            break;
        }
        case Tl::UpdatesTooLong:
            getDifference();
            break;
        default:
            applyUpdate(u);
            break;
        }
    } catch (const TlException &e) {
        emit log(QLatin1String("update handling failed: ") + e.message());
    }
}

bool TelegramSession::checkPts(const TlObject &u)
{
    // pts is the client's claim about how much of the update stream it has applied. An
    // update that continues the sequence is applied; a duplicate is dropped; a gap means
    // something was missed and getDifference fills it (delivering this update too).
    if (!u.has("pts")) return true;
    int pts = u.intOr("pts");
    int count = u.intOr("pts_count", 0);
    if (m_updateState.pts == 0) { m_updateState.pts = pts; m_stateDirty = true; return true; }
    if (m_updateState.pts + count == pts) { m_updateState.pts = pts; m_stateDirty = true; return true; }
    if (m_updateState.pts + count > pts) return false;         // already applied
    emit log(QString::fromLatin1("pts gap: have %1, update %2 (+%3)").arg(m_updateState.pts).arg(pts).arg(count));
    getDifference();
    return false;
}

void TelegramSession::applyUpdate(const TlObject &u)
{
    if (u.isNull()) return;
    switch (u.ctor()) {
    case Tl::UpdateNewMessage: {
        if (!checkPts(u)) return;
        TlObject o = u.obj("message");
        TgMessage m = TgApi::readMessage(o);
        if (m.service && o.has("action")) m.note = TgApi::describeAction(o.obj("action"), m_peers);
        applyMessage(m, false);
        break;
    }
    case Tl::UpdateNewChannelMessage: {
        // Channels keep their own pts; the message is applied and de-duplicated by id.
        TlObject o = u.obj("message");
        TgMessage m = TgApi::readMessage(o);
        if (m.service && o.has("action")) m.note = TgApi::describeAction(o.obj("action"), m_peers);
        applyMessage(m, false);
        break;
    }
    case Tl::UpdateEditMessage:
    case Tl::UpdateEditChannelMessage: {
        if (u.ctor() == Tl::UpdateEditMessage && !checkPts(u)) return;
        TlObject o = u.obj("message");
        TgMessage m = TgApi::readMessage(o);
        fillSender(m);
        emit messageEdited(m);
        int di = dialogIndex(m.peer);
        if (di >= 0 && m_dialogs.at(di).topMessageId == m.id) {
            m_dialogs[di].lastText = m.text.isEmpty() ? m.note : m.text;
            emit dialogChanged(m.peer);
        }
        break;
    }
    case Tl::UpdateDeleteMessages: {
        if (!checkPts(u)) return;
        QVariantList ids = u.vec("messages");
        QList<int> list;
        for (int i = 0; i < ids.size(); ++i) list.append(ids.at(i).toInt());
        emit messagesDeleted(TgPeer(), list);       // the peer is not named; models match by id
        break;
    }
    case Tl::UpdateUserTyping:
        emit typing(TgPeer(TgPeer::User, u.longOr("user_id")), u.longOr("user_id"));
        break;
    case Tl::UpdateChatUserTyping:
        emit typing(TgPeer(TgPeer::Chat, u.longOr("chat_id")), TgApi::readPeer(u.obj("from_id")).id);
        break;
    case Tl::UpdateChannelUserTyping:
        emit typing(TgPeer(TgPeer::Channel, u.longOr("channel_id")), TgApi::readPeer(u.obj("from_id")).id);
        break;
    case Tl::UpdateUserStatus: {
        qint64 id = u.longOr("user_id");
        m_peers.setUserStatus(id, u.obj("status"));
        emit peerChanged(TgPeer(TgPeer::User, id));
        break;
    }
    case Tl::UpdateUserName: {
        qint64 id = u.longOr("user_id");
        if (m_peers.contains(TgPeer(TgPeer::User, id))) {
            TgPeerInfo info = m_peers.info(TgPeer(TgPeer::User, id));
            info.firstName = u.str("first_name");
            info.lastName = u.str("last_name");
            QString name = (info.firstName + QLatin1Char(' ') + info.lastName).trimmed();
            if (!name.isEmpty()) info.title = name;
            m_peers.put(info);
            emit peerChanged(info.peer);
            if (dialogIndex(info.peer) >= 0) emit dialogChanged(info.peer);
        }
        break;
    }
    case Tl::UpdateReadHistoryInbox: {
        if (!checkPts(u)) return;
        TgPeer p = TgApi::readPeer(u.obj("peer"));
        int di = dialogIndex(p);
        if (di >= 0) {
            m_dialogs[di].unreadCount = u.intOr("still_unread_count");
            m_dialogs[di].readInboxMaxId = u.intOr("max_id");
            emit dialogChanged(p);
        }
        emit readInbox(p, u.intOr("max_id"));
        break;
    }
    case Tl::UpdateReadChannelInbox: {
        TgPeer p(TgPeer::Channel, u.longOr("channel_id"));
        int di = dialogIndex(p);
        if (di >= 0) {
            m_dialogs[di].unreadCount = u.intOr("still_unread_count");
            m_dialogs[di].readInboxMaxId = u.intOr("max_id");
            emit dialogChanged(p);
        }
        emit readInbox(p, u.intOr("max_id"));
        break;
    }
    case Tl::UpdateReadHistoryOutbox: {
        if (!checkPts(u)) return;
        TgPeer p = TgApi::readPeer(u.obj("peer"));
        int di = dialogIndex(p);
        if (di >= 0) m_dialogs[di].readOutboxMaxId = u.intOr("max_id");
        emit readOutbox(p, u.intOr("max_id"));
        break;
    }
    case Tl::UpdateReadChannelOutbox: {
        TgPeer p(TgPeer::Channel, u.longOr("channel_id"));
        int di = dialogIndex(p);
        if (di >= 0) m_dialogs[di].readOutboxMaxId = u.intOr("max_id");
        emit readOutbox(p, u.intOr("max_id"));
        break;
    }
    case Tl::UpdateNotifySettings: {
        TlObject np = u.obj("peer");
        if (!np.has("peer")) break;
        TgPeer p = TgApi::readPeer(np.obj("peer"));
        int di = dialogIndex(p);
        if (di >= 0) {
            m_dialogs[di].mutedUntil = u.obj("notify_settings").intOr("mute_until");
            emit dialogChanged(p);
        }
        break;
    }
    case Tl::UpdateLoginToken:
        if (m_state == LoggingIn && !m_passwordNeeded && !m_moved) exportToken();
        break;
    case Tl::UpdateFolderPeers: {
        // Chats moved into or out of the Archive (folder 1) on this or another device.
        QVariantList fps = u.vec("folder_peers");
        for (int i = 0; i < fps.size(); ++i) {
            int folderId = 0;
            QString key = TgApi::folderPeerKey(TlSchema::toObject(fps.at(i)), folderId);
            if (key.isEmpty()) continue;
            setArchived(TgPeer::fromKey(key), folderId == 1);
        }
        break;
    }
    case Tl::UpdateDialogFilter:
    case Tl::UpdateDialogFilters:
    case Tl::UpdateDialogFilterOrder:
        // A folder was added, changed, removed or reordered: re-read the set.
        loadFolders();
        break;
    case Tl::UpdateEncryption:
        handleEncryptedChat(u.obj("chat"));
        break;
    case Tl::UpdateNewEncryptedMessage:
        handleEncryptedMessage(u.obj("message"), u.intOr("date", unixNow()));
        break;
    case Tl::UpdateEncryptedMessagesRead:
    case Tl::UpdateEncryptedChatTyping:
        break;
    case Tl::UpdateChannelTooLong:
        // A channel this client cannot catch up incrementally; the list refresh shows the
        // newest state.
        refreshDialogs();
        break;
    default:
        if (u.has("pts") && !u.has("channel_id")) checkPts(u);   // keep the sequence moving
        break;
    }
}

void TelegramSession::touchDialog(const TgMessage &m)
{
    // A message to an archived chat stays in the Archive list; it does not resurface on the
    // main screen (Telegram keeps archived chats archived until the user acts).
    bool archived = m_archivedPeers.contains(m.peer.key());
    QList<TgDialog> &list = archived ? m_archived : m_dialogs;
    int di = dialogIndexIn(list, m.peer);
    if (di < 0) {
        TgDialog d;
        d.peer = m_peers.withHash(m.peer);
        d.archived = archived;
        list.append(d);
        di = list.size() - 1;
    }
    TgDialog &d = list[di];
    if (m.id > d.topMessageId) {
        d.topMessageId = m.id;
        d.topMessageDate = m.date;
        d.lastText = m.text.isEmpty() ? m.note : m.text;
        d.lastOut = m.out;
        d.lastFromId = m.fromId;
        if (!m.out && m.id > d.readInboxMaxId) d.unreadCount++;
    }
    if (archived) { emit archiveChanged(); }
    else { sortDialogs(); emit dialogsChanged(); }
}

void TelegramSession::fillSender(TgMessage &m) const
{
    // Messages in one-to-one chats and Saved Messages carry no from_id: the sender is the
    // other party, or ourselves when the message is outgoing.
    if (m.fromId == 0 && m.peer.kind == TgPeer::User) m.fromId = m.out ? m_selfId : m.peer.id;
    // Saved Messages: the server does not flag our own messages as outgoing there.
    if (m_selfId && m.fromId == m_selfId) m.out = true;
}

void TelegramSession::applyMessage(const TgMessage &msg, bool fromDifference)
{
    Q_UNUSED(fromDifference);
    if (msg.peer.isNull()) return;
    TgMessage m = msg;
    fillSender(m);
    // A pushed update and the follow-up getDifference both carry the same message; deliver
    // it once, or every message-driven action (rows, notifications) happens twice.
    if (m.id > 0) {
        QString key = m.peer.key() + QLatin1Char(':') + QString::number(m.id);
        if (m_recentSeen.contains(key)) { touchDialog(m); return; }
        m_recentSeen.append(key);
        while (m_recentSeen.size() > 200) m_recentSeen.removeFirst();
    }
    touchDialog(m);
    emit messageReceived(m);
}

// -- files ------------------------------------------------------------------------------------------------------

MtprotoClient *TelegramSession::clientForDc(int dcId)
{
    if (dcId == 0 || dcId == m_dcId) return m_client;
    if (m_dcLinks.contains(dcId)) return m_dcLinks[dcId].client;
    // A key per datacenter, and an authorisation carried over from the home one: the
    // file servers only answer sessions the account has been imported into.
    DcLink link;
    link.dcId = dcId;
    link.client = new MtprotoClient(this);
    link.client->setInfo(m_info);
    link.client->setProxy(m_proxy);
    link.client->setProperty("dcId", dcId);
    connect(link.client, SIGNAL(connected()), this, SLOT(onDcConnected()));
    connect(link.client, SIGNAL(disconnected(QString)), this, SLOT(onDcDisconnected(QString)));
    connect(link.client, SIGNAL(rpcResult(quint64,QByteArray)), this, SLOT(onRpcResult(quint64,QByteArray)));
    connect(link.client, SIGNAL(rpcError(quint64,int,QString)), this, SLOT(onRpcError(quint64,int,QString)));
    connect(link.client, SIGNAL(log(QString)), this, SIGNAL(log(QString)));
    m_dcLinks.insert(dcId, link);
    emit log(QString::fromLatin1("opening dc%1 for files (%2 key)").arg(dcId).arg(m_dcKeys.contains(dcId) ? QLatin1String("stored") : QLatin1String("new")));
    link.client->connectToDc(TelegramServers::hostFor(dcId), TelegramServers::DefaultPort, m_dcKeys.value(dcId));
    return link.client;
}

void TelegramSession::onDcConnected()
{
    MtprotoClient *client = qobject_cast<MtprotoClient *>(sender());
    if (!client) return;
    int dc = client->property("dcId").toInt();
    if (!m_dcLinks.contains(dc)) return;
    if (!m_dcKeys.contains(dc) || m_dcKeys.value(dc).keyId != client->authKey().keyId) {
        m_dcKeys.insert(dc, client->authKey());
        m_stateDirty = true;
        saveSessionFile();
    }
    if (m_dcLinks[dc].importing) return;
    m_dcLinks[dc].importing = true;
    Request rq;
    rq.offsetId = dc;
    send(ExportAuthorization, TgApi::exportAuthorization(dc), rq);
}

void TelegramSession::onDcDisconnected(const QString &reason)
{
    MtprotoClient *client = qobject_cast<MtprotoClient *>(sender());
    if (!client) return;
    int dc = client->property("dcId").toInt();
    emit log(QString::fromLatin1("dc%1 link lost: %2").arg(dc).arg(reason));
    failTransfersOnDc(dc, reason);
    m_dcLinks.remove(dc);
    client->deleteLater();
}

void TelegramSession::dcAuthorized(int dcId)
{
    if (!m_dcLinks.contains(dcId)) return;
    m_dcLinks[dcId].authorized = true;
    m_dcLinks[dcId].importing = false;
    emit log(QString::fromLatin1("dc%1 authorised for files").arg(dcId));
    pumpDownloads();
}

int TelegramSession::addDownload(int dcId, const QByteArray &location, const QString &targetPath, qint64 total)
{
    Download d;
    d.jobId = m_nextJobId++;
    d.dcId = dcId;
    d.location = location;
    d.path = targetPath;
    d.total = total;
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    d.file = new QFile(targetPath + QLatin1String(".part"), this);
    if (!d.file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete d.file;
        d.file = 0;
        int id = d.jobId;
        QTimer::singleShot(0, this, SLOT(onSaveTimer()));
        emit downloadFailed(id, tr("could not create the file"));
        return id;
    }
    m_downloads.insert(d.jobId, d);
    m_downloadOrder.append(d.jobId);
    pumpDownloads();
    return d.jobId;
}

int TelegramSession::downloadFile(const TgMedia &media, const QString &sizeType, const QString &targetPath)
{
    qint64 total = sizeType.isEmpty() || sizeType == media.bigSizeType ? media.fileSize : 0;
    return addDownload(media.dcId, TgApi::fileLocation(media, sizeType), targetPath, total);
}

int TelegramSession::downloadPeerPhoto(const TgPeer &peer, qint64 photoId, int dcId, const QString &targetPath)
{
    return addDownload(dcId, TgApi::peerPhotoLocation(m_peers.withHash(peer), photoId), targetPath, 0);
}

void TelegramSession::cancelDownload(int jobId)
{
    if (!m_downloads.contains(jobId)) return;
    Download d = m_downloads.take(jobId);
    m_downloadOrder.removeAll(jobId);
    if (d.file) { d.file->close(); d.file->remove(); delete d.file; }
}

void TelegramSession::pumpDownloads()
{
    int active = 0;
    for (int i = 0; i < m_downloadOrder.size(); ++i)
        if (m_downloads.value(m_downloadOrder.at(i)).active) ++active;
    for (int i = 0; i < m_downloadOrder.size() && active < MaxActiveDownloads; ++i) {
        int id = m_downloadOrder.at(i);
        if (!m_downloads.contains(id)) continue;
        Download &d = m_downloads[id];
        if (d.active) continue;
        if (!m_client->isReady()) return;
        MtprotoClient *client = clientForDc(d.dcId);
        bool ready = client == m_client || (m_dcLinks.contains(d.dcId) && m_dcLinks[d.dcId].authorized && client->isReady());
        if (!ready) continue;              // the link is being built; dcAuthorized() pumps again
        d.active = true;
        ++active;
        requestChunk(d, client);
    }
}

void TelegramSession::requestChunk(Download &d, MtprotoClient *client)
{
    Request rq;
    rq.offsetId = d.jobId;
    sendOn(client, GetFile, TgApi::getFile(d.location, d.offset, ChunkSize), rq);
}

void TelegramSession::finishDownload(int jobId, const QString &error)
{
    if (!m_downloads.contains(jobId)) return;
    Download d = m_downloads.take(jobId);
    m_downloadOrder.removeAll(jobId);
    if (d.file) {
        d.file->close();
        if (error.isEmpty() && d.offset > 0) {
            QFile::remove(d.path);
            d.file->rename(d.path);
        } else {
            d.file->remove();
        }
        delete d.file;
    }
    if (error.isEmpty() && d.offset > 0) emit downloadFinished(jobId, d.path);
    else emit downloadFailed(jobId, error.isEmpty() ? tr("the file is empty") : error);
    pumpDownloads();
}

void TelegramSession::failTransfersOnDc(int dcId, const QString &error)
{
    QList<int> ids = m_downloads.keys();
    for (int i = 0; i < ids.size(); ++i)
        if (m_downloads.value(ids.at(i)).dcId == dcId) finishDownload(ids.at(i), error);
}

void TelegramSession::failAllTransfers(const QString &error)
{
    QList<int> ids = m_downloads.keys();
    for (int i = 0; i < ids.size(); ++i) finishDownload(ids.at(i), error);
    QList<qint64> ups = m_uploads.keys();
    for (int i = 0; i < ups.size(); ++i) finishUpload(ups.at(i), error);
}

qint64 TelegramSession::sendFile(const TgPeer &peer, const QString &filePath, bool asPhoto, const QString &caption)
{
    Upload u;
    u.randomId = qint64(Crypto::randomUInt64());
    u.peer = m_peers.withHash(peer);
    u.path = filePath;
    u.fileName = QFileInfo(filePath).fileName();
    u.caption = caption;
    u.asPhoto = asPhoto;
    u.file = new QFile(filePath, this);
    if (!u.file->open(QIODevice::ReadOnly) || u.file->size() <= 0) {
        delete u.file;
        qint64 id = u.randomId;
        m_uploads.insert(id, Upload());
        finishUpload(id, tr("could not read the file"));
        return id;
    }
    u.size = u.file->size();
    u.big = u.size > BigFileThreshold;
    u.parts = int((u.size + PartSize - 1) / PartSize);
    u.fileId = qint64(Crypto::randomUInt64());
    if (u.parts > 4000) {
        delete u.file;
        qint64 id = u.randomId;
        m_uploads.insert(id, Upload());
        finishUpload(id, tr("the file is too large"));
        return id;
    }
    m_uploads.insert(u.randomId, u);
    if (!m_client->isReady()) { finishUpload(u.randomId, tr("Not connected.")); return u.randomId; }
    sendNextPart(m_uploads[u.randomId]);
    return u.randomId;
}

qint64 TelegramSession::sendVoice(const TgPeer &peer, const QString &oggPath, int durationSec, const QByteArray &waveform)
{
    Upload u;
    u.randomId = qint64(Crypto::randomUInt64());
    u.peer = m_peers.withHash(peer);
    u.path = oggPath;
    u.fileName = QLatin1String("voice.ogg");
    u.voice = true;
    u.durationSec = durationSec;
    u.waveform = waveform;
    u.file = new QFile(oggPath, this);
    if (!u.file->open(QIODevice::ReadOnly) || u.file->size() <= 0) {
        delete u.file;
        qint64 id = u.randomId;
        m_uploads.insert(id, Upload());
        finishUpload(id, tr("could not read the recording"));
        return id;
    }
    u.size = u.file->size();
    u.big = u.size > BigFileThreshold;
    u.parts = int((u.size + PartSize - 1) / PartSize);
    u.fileId = qint64(Crypto::randomUInt64());
    m_uploads.insert(u.randomId, u);
    if (!m_client->isReady()) { finishUpload(u.randomId, tr("Not connected.")); return u.randomId; }
    sendNextPart(m_uploads[u.randomId]);
    return u.randomId;
}

void TelegramSession::sendNextPart(Upload &u)
{
    QByteArray bytes = u.file->read(PartSize);
    Request rq;
    rq.peer = u.peer;
    rq.randomId = u.randomId;
    send(SaveFilePart, TgApi::saveFilePart(u.fileId, u.nextPart, u.parts, u.big, bytes), rq);
    ++u.nextPart;
}

void TelegramSession::finishUpload(qint64 randomId, const QString &error)
{
    if (!m_uploads.contains(randomId)) return;
    Upload u = m_uploads.take(randomId);
    if (u.file) { u.file->close(); delete u.file; }
    // The peer is empty for a job that never started; the model matches by random id.
    emit messageFailed(u.peer, randomId, error);
}


// -- secret (end-to-end) chats -------------------------------------------------------------------------------

QString TelegramSession::secretFilePath() const
{
    return QFileInfo(m_sessionFile).absolutePath() + QLatin1String("/secretchats.dat");
}

void TelegramSession::ensureDhConfig()
{
    if (m_dhReady || !m_client->isReady()) return;
    send(GetDhConfig, TgApi::getDhConfig(m_dhVersion, 256));
}

TgSecretChat TelegramSession::lightSecret(const SecretChat *c) const
{
    TgSecretChat t;
    t.id = c->chatId();
    t.peerUserId = c->peerUserId();
    t.state = int(c->state());
    t.isCreator = c->isCreator();
    t.ttl = c->ttl();
    t.keyHash = c->keyHash();
    return t;
}

QList<TgSecretChat> TelegramSession::secretChats() const
{
    QList<TgSecretChat> out;
    for (QHash<int, SecretChat *>::const_iterator it = m_secretChats.constBegin(); it != m_secretChats.constEnd(); ++it)
        if (it.value()->state() != SecretChat::Discarded) out.append(lightSecret(it.value()));
    return out;
}

TgSecretChat TelegramSession::secretChat(int id) const
{
    SecretChat *c = m_secretChats.value(id, 0);
    return c ? lightSecret(c) : TgSecretChat();
}

QByteArray TelegramSession::secretKeyHash(int id) const
{
    SecretChat *c = m_secretChats.value(id, 0);
    return c ? c->keyHash() : QByteArray();
}

void TelegramSession::appendSecretMessage(int id, qint64 randomId, const QString &text, int date, bool out, int ttl)
{
    // Telegram redelivers unacknowledged encrypted messages, so drop a random id we have
    // already seen in this chat (our own echo is seen first, then the server delivery).
    if (randomId != 0) {
        QSet<qint64> &seen = m_secretSeen[id];
        if (seen.contains(randomId)) return;
        seen.insert(randomId);
    }
    TgSecretMsg m;
    m.randomId = randomId;
    m.text = text;
    m.date = date;
    m.out = out;
    m.fromId = out ? m_selfId : 0;
    m.ttl = ttl;
    // Our own outgoing message starts self-destructing as soon as it is sent; an incoming
    // one starts when it is shown (the model calls startSecretExpiry then).
    if (ttl > 0 && out) { m.expiresAt = unixNow() + ttl; ensureSecretExpiryTimer(); }
    m_secretHistory[id].append(m);
    emit secretMessageReceived(id, randomId, text, date, out, ttl);
}

void TelegramSession::startSecretExpiry(int id, qint64 randomId)
{
    QList<TgSecretMsg> &buf = m_secretHistory[id];
    for (int i = 0; i < buf.size(); ++i)
        if (buf.at(i).randomId == randomId && buf.at(i).ttl > 0 && buf.at(i).expiresAt == 0) {
            buf[i].expiresAt = unixNow() + buf.at(i).ttl;
            ensureSecretExpiryTimer();
            return;
        }
}

void TelegramSession::ensureSecretExpiryTimer()
{
    if (!m_secretExpiryTimer->isActive()) m_secretExpiryTimer->start();
}

void TelegramSession::onSecretExpiryTick()
{
    int now = unixNow();
    bool anyPending = false;
    for (QHash<int, QList<TgSecretMsg> >::iterator it = m_secretHistory.begin(); it != m_secretHistory.end(); ++it) {
        QList<TgSecretMsg> &buf = it.value();
        for (int i = buf.size() - 1; i >= 0; --i) {
            if (buf.at(i).expiresAt <= 0) continue;
            if (now >= buf.at(i).expiresAt) {
                qint64 rid = buf.at(i).randomId;
                int id = it.key();
                buf.removeAt(i);
                emit secretMessageExpired(id, rid);
            } else {
                anyPending = true;
            }
        }
    }
    if (!anyPending) m_secretExpiryTimer->stop();
}

void TelegramSession::requestSecretChat(const TgPeer &user)
{
    TgPeer u = m_peers.withHash(user);
    if (u.kind != TgPeer::User || u.accessHash == 0) { emit notice(tr("Secret chats can only be opened with a person.")); return; }
    if (!m_dhReady) { m_secretRequestQueue.append(u); ensureDhConfig(); return; }
    SecretChat *chat = new SecretChat;
    int rid = qint32(Crypto::randomUInt64());
    QByteArray gA = chat->startAsCreator(u.id, rid, m_dhG, m_dhP);
    Request rq;
    rq.secret = chat;
    send(RequestEncryption, TgApi::requestEncryption(u, rid, gA), rq);
    emit log(QString::fromLatin1("secret: requested a chat with user %1").arg(u.id));
}

void TelegramSession::acceptSecretChat(int id)
{
    SecretChat *chat = m_secretChats.value(id, 0);
    if (!chat || chat->state() != SecretChat::RequestedToMe) return;
    if (!m_dhReady) { if (!m_secretAcceptQueue.contains(id)) m_secretAcceptQueue.append(id); ensureDhConfig(); return; }
    QByteArray gB = chat->acceptAsParticipant(chat->chatId(), chat->accessHash(), chat->adminId(), m_selfId,
                                              chat->incomingGa(), m_dhG, m_dhP);
    Request rq;
    rq.secretChatId = id;
    send(AcceptEncryption, TgApi::acceptEncryption(chat->chatId(), chat->accessHash(), gB, chat->keyFingerprint()), rq);
    saveSecrets();
}

void TelegramSession::discardSecretChat(int id)
{
    emit log(QString::fromLatin1("secret: discarding chat %1 (sending discardEncryption)").arg(id));
    SecretChat *chat = m_secretChats.value(id, 0);
    if (m_client->isReady()) send(DiscardEncryption, TgApi::discardEncryption(id));
    if (chat) { m_secretChats.remove(id); delete chat; }
    saveSecrets();
    emit secretChatDiscarded(id);
    emit secretChatsChanged();
}

void TelegramSession::sendSecretService(SecretChat *chat, const QByteArray &body)
{
    if (!chat || chat->state() != SecretChat::Ready || !m_client->isReady()) return;
    qint64 rid = qint64(Crypto::randomUInt64());
    QByteArray data = chat->encryptMessage(body);
    Request rq;
    rq.secretChatId = chat->chatId();
    rq.randomId = rid;
    send(SendEncrypted, TgApi::sendEncryptedService(chat->chatId(), chat->accessHash(), rid, data), rq);
    saveSecrets();
}

qint64 TelegramSession::sendSecretText(int id, const QString &text)
{
    SecretChat *chat = m_secretChats.value(id, 0);
    if (!chat || chat->state() != SecretChat::Ready) { emit secretMessageFailed(id, 0, tr("The secret chat is not ready.")); return 0; }
    qint64 rid = qint64(Crypto::randomUInt64());
    QByteArray data = chat->encryptMessage(SecretApi::textMessage(rid, chat->ttl(), text));
    Request rq;
    rq.secretChatId = id;
    rq.randomId = rid;
    send(SendEncrypted, TgApi::sendEncrypted(id, chat->accessHash(), rid, data), rq);
    saveSecrets();
    // Optimistic local echo, matched to the ack by random id.
    appendSecretMessage(id, rid, text, unixNow(), true, chat->ttl());
    return rid;
}

void TelegramSession::setSecretTtl(int id, int seconds)
{
    SecretChat *chat = m_secretChats.value(id, 0);
    if (!chat) return;
    chat->setTtl(seconds);
    qint64 rid = qint64(Crypto::randomUInt64());
    sendSecretService(chat, SecretApi::setTtl(rid, seconds));
    saveSecrets();
    emit secretChatsChanged();
}

void TelegramSession::handleEncryptedChat(const TlObject &chat)
{
    if (chat.isNull()) return;
    int id = chat.intOr("id");
    switch (chat.ctor()) {
    case Tl::EncryptedChatRequested: {
        emit log(QString::fromLatin1("secret: incoming request for chat %1").arg(id));
        if (m_secretChats.contains(id)) return;
        SecretChat *c = new SecretChat;
        c->setIncomingRequest(id, chat.longOr("access_hash"), chat.longOr("admin_id"),
                              chat.longOr("participant_id"), chat.bytes("g_a"));
        m_secretChats.insert(id, c);
        saveSecrets();
        emit secretChatRequested(id, c->adminId());
        emit secretChatsChanged();
        break;
    }
    case Tl::EncryptedChat: {
        SecretChat *c = m_secretChats.value(id, 0);
        if (!c) return;
        if (c->state() == SecretChat::RequestedByMe) {
            // My request was accepted: finish the key from their g_b.
            emit log(QString::fromLatin1("secret: chat %1 accepted by peer, finishing key").arg(id));
            bool ok = c->finishAsCreator(id, chat.longOr("access_hash"), chat.longOr("admin_id"),
                                         chat.longOr("participant_id"), chat.bytes("g_a_or_b"), chat.longOr("key_fingerprint"));
            if (!ok) { emit log(QLatin1String("secret: key fingerprint MISMATCH, discarding")); discardSecretChat(id); return; }
            emit log(QString::fromLatin1("secret: chat %1 key fingerprint OK").arg(id));
        } else if (c->state() != SecretChat::Ready) {
            return;
        }
        c->setAccess(id, chat.longOr("access_hash"));
        // The very first out message on a layer >= 46 chat announces our layer.
        if (c->outSeqCount() == 0) { emit log(QString::fromLatin1("secret: chat %1 ready, sending notifyLayer(%2)").arg(id).arg(Tl::SecretLayer)); sendSecretService(c, SecretApi::notifyLayer(qint64(Crypto::randomUInt64()), Tl::SecretLayer)); }
        saveSecrets();
        emit secretChatReady(id);
        emit secretChatsChanged();
        break;
    }
    case Tl::EncryptedChatDiscarded: {
        emit log(QString::fromLatin1("secret: chat %1 was DISCARDED by the peer/server").arg(id));
        SecretChat *c = m_secretChats.value(id, 0);
        if (c) { m_secretChats.remove(id); delete c; saveSecrets(); }
        emit secretChatDiscarded(id);
        emit secretChatsChanged();
        break;
    }
    default:
        break;   // encryptedChatWaiting / empty: nothing to do
    }
}

void TelegramSession::handleEncryptedMessage(const TlObject &message, int date)
{
    if (message.isNull()) return;
    int id = message.intOr("chat_id");
    SecretChat *chat = m_secretChats.value(id, 0);
    if (!chat || chat->state() != SecretChat::Ready) {
        emit log(QString::fromLatin1("secret: message for unknown/not-ready chat %1").arg(id));
        return;
    }
    try {
        int senderSeq = -1;
        QByteArray body = chat->decryptMessage(message.bytes("bytes"), senderSeq);
        SecretContent c = SecretApi::read(body);
        int when = message.intOr("date", date);
        switch (c.kind) {
        case SecretContent::Text:
            appendSecretMessage(id, c.randomId, c.text, when, false, c.ttl);
            break;
        case SecretContent::SetTtl:
            chat->setTtl(c.ttl);
            emit secretChatsChanged();
            break;
        case SecretContent::NotifyLayer:
            emit log(QString::fromLatin1("secret: chat %1 peer advertised layer %2").arg(id).arg(c.layer));
            break;
        case SecretContent::FlushHistory:
        case SecretContent::Delete:
        case SecretContent::Read:
        case SecretContent::Typing:
        case SecretContent::Unsupported:
            break;
        }
        saveSecrets();
    } catch (const TlException &e) {
        emit log(QLatin1String("secret: could not decrypt a message: ") + e.message());
    }
}

// -- persistence -----------------------------------------------------------------------------------------

void TelegramSession::saveSecrets()
{
    if (m_sessionFile.isEmpty()) return;
    QString path = secretFilePath();
    if (m_secretChats.isEmpty()) { QFile::remove(path); return; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_4_7);
    s << quint32(0x53474d53) << qint32(m_secretChats.size());   // "SGMS"
    for (QHash<int, SecretChat *>::const_iterator it = m_secretChats.constBegin(); it != m_secretChats.constEnd(); ++it)
        it.value()->save(s);
}

void TelegramSession::loadSecrets()
{
    qDeleteAll(m_secretChats);
    m_secretChats.clear();
    QFile f(secretFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QDataStream s(&f);
    s.setVersion(QDataStream::Qt_4_7);
    quint32 magic = 0;
    qint32 count = 0;
    s >> magic >> count;
    if (magic != 0x53474d53) return;
    for (int i = 0; i < count && s.status() == QDataStream::Ok; ++i) {
        SecretChat *c = new SecretChat;
        if (c->load(s) && c->chatId() != 0) m_secretChats.insert(c->chatId(), c);
        else delete c;
    }
}
