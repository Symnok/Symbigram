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

    void showPopupL(const QString &title, const QString &text)
    {
        // Long popup with the lights on and the confirmation tone; tapping it launches
        // (brings forward) this application through its UID.
        CAknDiscreetPopup::ShowGlobalPopupL(ptr(title), ptr(text), KAknsIIDNone, KNullDesC, 0, 0,
            KAknDiscreetPopupDurationLong | KAknDiscreetPopupLightsOn | KAknDiscreetPopupConfirmationTone,
            0, NULL, TUid::Uid(SGM_UID3));
    }

    void vibrateL(int ms)
    {
        CHWRMVibra *v = CHWRMVibra::NewLC();
        v->StartVibraL(ms);
        CleanupStack::PopAndDestroy(v);
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
    : QObject(parent), m_enabled(true), m_vibrate(true), m_popups(true), m_pending(0), m_query(0)
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
#endif
}

void Notifier::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    showPending();   // takes the query and the envelope down, or brings them back
}

void Notifier::notify(const QString &title, const QString &text)
{
    if (!m_enabled) return;
#ifdef Q_OS_SYMBIAN
    if (m_popups) {
        QString t = title;
        QString b = text.simplified();
        if (b.size() > 120) b = b.left(117) + QLatin1String("...");
        TRAP_IGNORE(showPopupL(t, b));
    }
    if (m_vibrate) TRAP_IGNORE(vibrateL(400));
#else
    qDebug() << "NOTIFY" << title << ":" << text << (m_popups ? "" : "(popups off)") << (m_vibrate ? "" : "(vibrate off)");
#endif
}

void Notifier::vibrate(int ms)
{
    if (!m_enabled) return;
#ifdef Q_OS_SYMBIAN
    if (m_vibrate) TRAP_IGNORE(vibrateL(ms));
#else
    qDebug() << "VIBRATE" << ms;
#endif
}

QString Notifier::pendingText(int count)
{
    return count == 1 ? tr("Symbigram: new message") : tr("Symbigram: %1 new messages").arg(count);
}

void Notifier::setPendingCount(int count)
{
    if (count < 0) count = 0;
    if (count == m_pending) return;
    m_pending = count;
    showPending();
}

void Notifier::showPending()
{
    int count = m_enabled ? m_pending : 0;
#ifdef Q_OS_SYMBIAN
    PendingQuery *q = static_cast<PendingQuery *>(m_query);
    if (q) {
        if (count > 0) {
            TRAPD(err, q->ShowL(pendingText(count)));
            if (err != KErrNone) qWarning() << "notification query failed:" << err;
        } else {
            q->Cancel();
        }
    }
    TRAPD(err, setEnvelopeL(count > 0));
    if (err != KErrNone) qWarning() << "envelope indicator failed:" << err;
#else
    qDebug() << "NOTIFICATION" << pendingText(count);
#endif
}
