// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "notifier.h"

#include <QApplication>
#include <QDebug>
#include <QWidget>

#ifdef Q_OS_SYMBIAN
#include <akndiscreetpopup.h>
#include <AknGlobalConfirmationQuery.h>
#include <AknSmallIndicator.h>
#include <apgtask.h>
#include <avkon.hrh>
#include <avkon.rsg>
#include <coemain.h>
#include <e32std.h>
#include <hwrmvibra.h>
#include <mdaaudiotoneplayer.h>

#ifndef SGM_UID3
#define SGM_UID3 0xE4B1C2D3
#endif

namespace
{
    TPtrC ptr(const QString &s)
    {
        return TPtrC(reinterpret_cast<const TUint16 *>(s.utf16()), s.length());
    }

    /// Brings this application's window group to the front, whatever is over it.
    void raiseApplication()
    {
        CCoeEnv *env = CCoeEnv::Static();
        if (env) {
            TApaTask task(env->WsSession());
            task.SetWgId(env->RootWin().Identifier());
            task.BringToForeground();
        }
        QWidget *w = QApplication::activeWindow();
        if (!w) {
            QWidgetList tops = QApplication::topLevelWidgets();
            if (!tops.isEmpty()) w = tops.first();
        }
        if (w) { w->raise(); w->activateWindow(); }
    }

    /// The "N new messages" query shown over whatever is in front. It is a global query
    /// rather than a soft notification: the answer comes back to this process, so "Show"
    /// simply raises the application here instead of asking the view server to do it.
    class PendingQuery : public CActive
    {
    public:
        static PendingQuery *NewL()
        {
            PendingQuery *q = new (ELeave) PendingQuery();
            CleanupStack::PushL(q);
            q->iQuery = CAknGlobalConfirmationQuery::NewL();
            CleanupStack::Pop(q);
            return q;
        }
        ~PendingQuery()
        {
            Cancel();
            delete iQuery;
            delete iPrompt;
        }
        // the prompt must outlive the request: the notifier server reads it later
        void ShowL(const QString &text)
        {
            Cancel();
            delete iPrompt;
            iPrompt = 0;
            iPrompt = ptr(text).AllocL();
            iQuery->ShowConfirmationQueryL(iStatus, *iPrompt, R_AVKON_SOFTKEYS_SHOW_CANCEL);
            SetActive();
        }
    private:
        PendingQuery() : CActive(EPriorityStandard), iQuery(0), iPrompt(0) { CActiveScheduler::Add(this); }
        void RunL()
        {
            TInt key = iStatus.Int();
            if (key == EAknSoftkeyShow || key == EAknSoftkeyOk || key == EAknSoftkeyYes) raiseApplication();
            else if (key < 0 && key != KErrCancel) qWarning() << "notification query failed:" << key;
        }
        void DoCancel() { iQuery->CancelConfirmationQuery(); }
        CAknGlobalConfirmationQuery *iQuery;
        HBufC *iPrompt;
    };

    void showPopupL(const QString &title, const QString &text, bool withTone)
    {
        // Long popup with the lights on and (optionally) the confirmation tone; tapping it
        // launches (brings forward) this application through its UID.
        TUint flags = KAknDiscreetPopupDurationLong | KAknDiscreetPopupLightsOn;
        if (withTone) flags |= KAknDiscreetPopupConfirmationTone;
        CAknDiscreetPopup::ShowGlobalPopupL(ptr(title), ptr(text), KAknsIIDNone, KNullDesC, 0, 0,
            flags, 0, NULL, TUid::Uid(SGM_UID3));
    }

    // The discreet popup's confirmation tone has no volume control (it is a system sound), which
    // is why it is so quiet. For the louder levels we play our own tone through
    // CMdaAudioToneUtility, which does have SetVolume - and which, unlike the popup tone, sounds
    // whether or not a popup is shown. Prepared once and replayed for every alert.
    const TInt KToneHz = 1200;
    const TInt KToneUs = 200000;   // 0.2 s

    class ToneBeep : public MMdaAudioToneObserver
    {
    public:
        static ToneBeep *NewL()
        {
            ToneBeep *t = new (ELeave) ToneBeep();
            CleanupStack::PushL(t);
            t->ConstructL();
            CleanupStack::Pop(t);
            return t;
        }
        ~ToneBeep()
        {
            if (iTone) { iTone->CancelPlay(); delete iTone; }
        }
        // level 1 = loud, 2 = loudest. Silently does nothing until the tone is prepared.
        void Play(TInt aLevel)
        {
            if (!iReady || !iTone) return;
            if (iTone->State() != EMdaAudioToneUtilityPrepared) return;   // still playing the last one
            const TInt max = iTone->MaxVolume();
            TInt vol = aLevel >= 2 ? max : (max * 3) / 5;
            if (vol < 1) vol = 1;
            iTone->SetVolume(vol);
            iTone->Play();
        }
        TBool Ready() const { return iReady; }
    private:
        ToneBeep() : iTone(0), iReady(EFalse) {}
        void ConstructL()
        {
            iTone = CMdaAudioToneUtility::NewL(*this);
            iTone->PrepareToPlayTone(KToneHz, TTimeIntervalMicroSeconds(KToneUs));
        }
        void MatoPrepareComplete(TInt aError)
        {
            iReady = (aError == KErrNone);
            if (aError != KErrNone) qWarning() << "notification tone unavailable:" << aError;
        }
        void MatoPlayComplete(TInt /*aError*/) {}
        CMdaAudioToneUtility *iTone;
        TBool iReady;
    };

    // Keep the CHWRMVibra session alive: destroying it right after StartVibraL cancels the
    // vibration (the server drops it when the client session closes), so a throwaway object
    // vibrated for essentially 0 ms - which is why nothing was felt. Reuse one instance.
    void vibrateL(void *&vibra, int ms)
    {
        if (!vibra) vibra = CHWRMVibra::NewL();
        static_cast<CHWRMVibra *>(vibra)->StartVibraL(ms);
    }

    // The "new message" envelope in the status bar - the small indicator the messaging
    // application lights up.
    void setEnvelopeL(bool on)
    {
        CAknSmallIndicator *ind = CAknSmallIndicator::NewLC(TUid::Uid(EAknIndicatorEnvelope));
        ind->SetIndicatorStateL(on ? EAknIndicatorStateOn : EAknIndicatorStateOff);
        CleanupStack::PopAndDestroy(ind);
    }
}
#endif

Notifier::Notifier(QObject *parent)
    : QObject(parent), m_popQuery(false), m_pending(0), m_soundLevel(0), m_vibrateMs(400),
      m_query(0), m_vibra(0), m_tone(0)
{
#ifdef Q_OS_SYMBIAN
    PendingQuery *q = 0;
    TRAPD(err, q = PendingQuery::NewL());
    if (err != KErrNone) qWarning() << "notification query unavailable:" << err;
    m_query = q;
#endif
}

Notifier::~Notifier()
{
#ifdef Q_OS_SYMBIAN
    delete static_cast<PendingQuery *>(m_query);
    delete static_cast<CHWRMVibra *>(m_vibra);
    delete static_cast<ToneBeep *>(m_tone);
#endif
}

void Notifier::setSoundVolume(int level)
{
    m_soundLevel = (level < 0 || level > 2) ? 0 : level;
#ifdef Q_OS_SYMBIAN
    // The louder levels need our own tone player; create it once, on demand.
    if (m_soundLevel > 0 && !m_tone) {
        ToneBeep *t = 0;
        TRAPD(err, t = ToneBeep::NewL());
        if (err != KErrNone) qWarning() << "notification tone unavailable:" << err;
        m_tone = t;
    }
#endif
}

void Notifier::alert(const QString &title, const QString &text, bool showPopup, bool playSound, bool vibrate)
{
#ifdef Q_OS_SYMBIAN
    // Louder levels use our own tone (which works with or without a popup); the Low level -
    // and any level whose tone player failed to start - falls back to the popup's built-in tone.
    ToneBeep *beep = static_cast<ToneBeep *>(m_tone);
    const bool ownTone = playSound && m_soundLevel > 0 && beep && beep->Ready();
    if (showPopup) {
        QString t = title;
        QString b = text.simplified();
        if (b.size() > 120) b = b.left(117) + QLatin1String("...");
        TRAP_IGNORE(showPopupL(t, b, playSound && !ownTone));
    }
    if (ownTone) beep->Play(m_soundLevel);
    if (vibrate) TRAP_IGNORE(vibrateL(m_vibra, m_vibrateMs));
#else
    qDebug() << "ALERT" << title << ":" << text << (showPopup ? "(popup)" : "")
             << (playSound ? "(sound)" : "") << (playSound ? m_soundLevel : 0)
             << (vibrate ? "(vibrate)" : "") << (vibrate ? m_vibrateMs : 0);
#endif
}

void Notifier::vibrate(int ms)
{
#ifdef Q_OS_SYMBIAN
    TRAP_IGNORE(vibrateL(m_vibra, ms));
#else
    qDebug() << "VIBRATE" << ms;
#endif
}

QString Notifier::pendingText(int count)
{
    return count == 1 ? tr("Symbigram: new message") : tr("Symbigram: %1 new messages").arg(count);
}

void Notifier::setPendingCount(int count, bool popQuery)
{
    if (count < 0) count = 0;
    m_popQuery = popQuery;
    if (count == m_pending && !popQuery) return;
    m_pending = count;
    showPending();
}

void Notifier::showPending()
{
    int count = m_pending;
#ifdef Q_OS_SYMBIAN
    PendingQuery *q = static_cast<PendingQuery *>(m_query);
    if (q) {
        // The "N new messages" query is a pop-up: it is raised only when a popup should fire for
        // this message (m_popQuery). Otherwise leave whatever is showing, and take it down only
        // when nothing is pending. The passive envelope (and Pigler) are independent of all this.
        if (count > 0 && m_popQuery) {
            TRAPD(err, q->ShowL(pendingText(count)));
            if (err != KErrNone) qWarning() << "notification query failed:" << err;
        } else if (count <= 0) {
            q->Cancel();
        }
    }
    TRAPD(err, setEnvelopeL(count > 0));
    if (err != KErrNone) qWarning() << "envelope indicator failed:" << err;
    m_popQuery = false;
#else
    qDebug() << "NOTIFICATION" << pendingText(count) << (m_popQuery ? "(query)" : "");
    m_popQuery = false;
#endif
}
