// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "telegramsession.h"
#include "crypto.h"
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
      m_stateDirty(false), m_qrExpires(0), m_passwordNeeded(false), m_movedDc(0), m_loggingOut(false), m_selfId(0),
      m_dialogsHaveMore(false), m_dialogsLoading(false), m_differencePending(false), m_online(false), m_nextJobId(1)
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
    m_peers.clear();
    m_qrUrl.clear();
    m_qrToken.clear();
    m_passwordNeeded = false;
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
    exportToken();
}

void TelegramSession::exportToken()
{
    send(ExportToken, TgApi::exportLoginToken(m_info.apiId, m_info.apiHash));
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
    requestDialogs(0, 0, TgPeer(), false);
}

void TelegramSession::loadMoreDialogs()
{
    if (m_dialogsLoading || !m_dialogsHaveMore || m_dialogs.isEmpty()) return;
    // The next page starts after the oldest unpinned entry; the server orders by the date
    // of the last message and identifies the position by date, id and peer together.
    const TgDialog &last = m_dialogs.last();
    requestDialogs(last.topMessageDate, last.topMessageId, last.peer, true);
}

void TelegramSession::requestDialogs(int offsetDate, int offsetId, const TgPeer &offsetPeer, bool more)
{
    if (!m_client->isReady()) return;
    m_dialogsLoading = true;
    Request r;
    r.more = more;
    send(GetDialogs, TgApi::getDialogs(offsetDate, offsetId, offsetPeer, DialogPageSize), r);
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
            if (o.ctor() != Tl::MessagesDialogsNotModified) applyDialogs(TgApi::readDialogs(o, m_peers), req.more);
            if (m_state == Syncing) setState(Online);
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
    case GetState:
    case GetSelf:
    case ReadHistory:
    case SetTyping:
    case UpdateStatus:
    case DeleteHistory:
    case DeleteMessages:
    case UpdateNotifySettings:
    case GetUser:
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
    int di = dialogIndex(m.peer);
    if (di < 0) {
        TgDialog d;
        d.peer = m_peers.withHash(m.peer);
        m_dialogs.append(d);
        di = m_dialogs.size() - 1;
    }
    TgDialog &d = m_dialogs[di];
    if (m.id > d.topMessageId) {
        d.topMessageId = m.id;
        d.topMessageDate = m.date;
        d.lastText = m.text.isEmpty() ? m.note : m.text;
        d.lastOut = m.out;
        d.lastFromId = m.fromId;
        if (!m.out && m.id > d.readInboxMaxId) d.unreadCount++;
    }
    sortDialogs();
    emit dialogsChanged();
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
