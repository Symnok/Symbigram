// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "piglernotifier.h"

#ifdef Q_OS_SYMBIAN
#include "QPiglerAPI.h"
#endif

PiglerNotifier::PiglerNotifier(QObject *parent)
    : QObject(parent), m_available(false)
#ifdef Q_OS_SYMBIAN
    , m_api(0)
#endif
{
}

PiglerNotifier::~PiglerNotifier()
{
#ifdef Q_OS_SYMBIAN
    if (m_api) {
        m_api->removeAllNotifications();
        m_api->close();
        delete m_api;
        m_api = 0;
    }
#endif
}

bool PiglerNotifier::init(const QString &appName, int appUid)
{
#ifdef Q_OS_SYMBIAN
    if (m_api) return m_available;
    m_api = new QPiglerAPI(this);
    if (appUid) m_api->setAppId(appUid);   // so a tap can relaunch us if we are closed
    connect(m_api, SIGNAL(handleTap(qint32)), this, SLOT(onTap(qint32)));
    // Returns <0 on error (Pigler not installed / no server -> CreateSession fails), 0 when
    // connected, or a positive notification id if the app was launched by a tap. >=0 means the
    // connection is up. Any failure leaves the system untouched.
    qint32 res = m_api->init(appName);
    m_available = (res >= 0);
    if (!m_available) { delete m_api; m_api = 0; }
    return m_available;
#else
    Q_UNUSED(appName); Q_UNUSED(appUid);
    return false;
#endif
}

void PiglerNotifier::showMessage(const QString &peerKey, const QString &title, const QString &text)
{
#ifdef Q_OS_SYMBIAN
    if (!m_available || !m_api) return;
    if (m_ids.contains(peerKey)) {                 // one entry per chat: update it
        m_api->updateNotification(m_ids.value(peerKey), title, text);
        return;
    }
    qint32 id = m_api->createNotification(title, text);
    if (id > 0) {                                  // <=0: an error or the per-app limit; skip quietly
        m_api->setLaunchAppOnTap(id, true);
        m_ids.insert(peerKey, id);
        m_keys.insert(id, peerKey);
    }
#else
    Q_UNUSED(peerKey); Q_UNUSED(title); Q_UNUSED(text);
#endif
}

void PiglerNotifier::clear(const QString &peerKey)
{
#ifdef Q_OS_SYMBIAN
    if (!m_available || !m_api || !m_ids.contains(peerKey)) return;
    int id = m_ids.take(peerKey);
    m_keys.remove(id);
    m_api->removeNotification(id);
#else
    Q_UNUSED(peerKey);
#endif
}

void PiglerNotifier::clearAll()
{
#ifdef Q_OS_SYMBIAN
    if (m_available && m_api) m_api->removeAllNotifications();
#endif
    m_ids.clear();
    m_keys.clear();
}

void PiglerNotifier::onTap(qint32 notificationId)
{
    if (m_keys.contains(notificationId)) {
        QString key = m_keys.value(notificationId);
        // Pigler removes the tapped notification itself (remove-on-tap defaults to true).
        m_ids.remove(key);
        m_keys.remove(notificationId);
        emit tapped(key);
    }
}
