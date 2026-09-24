// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Status-bar ("envelope") notifications on Symbian Belle via the Pigler Notifications API
// (third_party/pigler, an IPC client to a separately-installed Pigler server). It is entirely
// optional: init() only succeeds on a Belle device that has Pigler installed; otherwise
// available() stays false and every call is a no-op, so notifications behave exactly as before
// (the legacy Notifier). Nothing here is used on S^3/Anna. Pigler notifications are per-chat
// (keyed by peerKey) so repeated messages update one entry, and tapping one opens that chat.
#ifndef PIGLERNOTIFIER_H
#define PIGLERNOTIFIER_H

#include <QHash>
#include <QObject>
#include <QString>

class QPiglerAPI;

class PiglerNotifier : public QObject
{
    Q_OBJECT
public:
    explicit PiglerNotifier(QObject *parent = 0);
    ~PiglerNotifier();

    /// Connects to the Pigler server. Returns false (and does nothing further) when Pigler is
    /// not present - the caller then just relies on the legacy Notifier.
    bool init(const QString &appName, int appUid);
    bool available() const { return m_available; }

    void showMessage(const QString &peerKey, const QString &title, const QString &text);
    void clear(const QString &peerKey);
    void clearAll();

signals:
    void tapped(const QString &peerKey);   // the user tapped a chat's status-bar notification

private slots:
    void onTap(qint32 notificationId);

private:
    bool m_available;
    QHash<QString, int> m_ids;    // peerKey -> Pigler notification id
    QHash<int, QString> m_keys;   // Pigler notification id -> peerKey
#ifdef Q_OS_SYMBIAN
    QPiglerAPI *m_api;
#endif
};

#endif // PIGLERNOTIFIER_H
