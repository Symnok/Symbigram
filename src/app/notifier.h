// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Tells the user about a message while another application is in front: on Symbian the
// "new message" envelope in the status bar (until the app is opened again), vibration, and
// optionally a discreet popup at the top of the screen. The app itself keeps running in
// the background with its socket open, so this is all a "service" needs to be here.
// Elsewhere it just logs.
#ifndef NOTIFIER_H
#define NOTIFIER_H

#include <QObject>
#include <QString>

class Notifier : public QObject
{
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = 0);
    ~Notifier();

    /// The master switch: off means no popup, no vibration, no "new messages" query and
    /// no envelope (the unread badges in the list are not affected).
    void setEnabled(bool on);
    void setVibrate(bool on) { m_vibrate = on; }

    /// Optionally shows the discreet popup (title = who, text = what) and always vibrates per the
    /// vibrate setting. The caller decides whether the popup shows (Off / first / every message).
    void notify(const QString &title, const QString &text, bool showPopup);
    /// Just the vibration, e.g. for an authorization request.
    void vibrate(int ms = 400);
    /// Tracks the unread count and lights the status-bar envelope. popQuery also raises the
    /// "N new messages" query (a global query with a "Show" softkey that brings the app forward);
    /// pass it true only when a popup should fire for this message.
    void setPendingCount(int count, bool popQuery = false);
    int pendingCount() const { return m_pending; }

private:
    void showPending();
    bool m_enabled;
    bool m_vibrate;
    bool m_popQuery;   // whether the next showPending() should raise the query
    int m_pending;
    static QString pendingText(int count);

    void *m_query;   // the global query and its active object (Symbian only)
};

#endif // NOTIFIER_H
