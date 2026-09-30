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

    /// Fires the enabled alert channels for one message: the discreet popup (title = who, text =
    /// what) if showPopup, the alert tone if playSound, and the vibration if vibrate. The three are
    /// independent; the caller decides each from its own Off/First/Every setting.
    void alert(const QString &title, const QString &text, bool showPopup, bool playSound, bool vibrate);
    /// Just the vibration, e.g. for an authorization request.
    void vibrate(int ms = 400);
    /// Sound volume: 0 = Low (the discreet popup's own quiet confirmation tone, as before),
    /// 1/2 = our own tone played at a controlled volume (and independent of the popup showing).
    void setSoundVolume(int level);
    /// How long the vibration runs, in milliseconds.
    void setVibrationMs(int ms) { m_vibrateMs = ms > 0 ? ms : 400; }
    /// Tracks the unread count and lights the status-bar envelope. popQuery also raises the
    /// "N new messages" query (a global query with a "Show" softkey that brings the app forward);
    /// pass it true only when a popup should fire for this message.
    void setPendingCount(int count, bool popQuery = false);
    int pendingCount() const { return m_pending; }

private:
    void showPending();
    bool m_popQuery;   // whether the next showPending() should raise the query
    int m_pending;
    static QString pendingText(int count);

    int m_soundLevel;  // 0 = popup's built-in tone, 1/2 = our own tone, louder
    int m_vibrateMs;   // vibration length

    void *m_query;   // the global query and its active object (Symbian only)
    void *m_vibra;   // a kept-alive CHWRMVibra session (Symbian only)
    void *m_tone;    // a kept-alive CMdaAudioToneUtility beep (Symbian only)
};

#endif // NOTIFIER_H
