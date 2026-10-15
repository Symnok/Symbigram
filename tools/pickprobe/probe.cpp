// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Capability probe for the phone's own image fetcher (MGFetch / the Photos picker).
//
// Symbigram's own call leaves with KErrPermissionDenied (-46) from a process holding
// NetworkServices + ReadUserData + WriteUserData + UserEnvironment. That is ambiguous: Symbian's
// loader demands a DLL's capability set be a SUPERSET of the loading process's, so -46 can mean
// either "this process has a capability the fetch plug-in lacks" or "this process is missing a
// capability the plug-in needs". Those have opposite fixes, so this source is built twice with
// opposite capability sets (pickmin.pro / pickmax.pro) and whichever one shows a picker wins.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QString>
#include <QTextStream>

#include <e32base.h>
#include <badesca.h>
#include <mgfetch.h>

namespace
{
    void pickL(QString &out)
    {
        CDesCArrayFlat *files = new (ELeave) CDesCArrayFlat(1);
        CleanupStack::PushL(files);
        const TBool chosen = MGFetch::RunL(*files, EImageFile, EFalse);
        if (chosen && files->Count() > 0) {
            const TPtrC p = (*files)[0];
            out = QString::fromUtf16(reinterpret_cast<const ushort *>(p.Ptr()), p.Length());
        }
        CleanupStack::PopAndDestroy(files);
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QString picked;
    TRAPD(err, pickL(picked));

    QString result;
    if (err != KErrNone) result = QLatin1String("ERR ") + QString::number(err);
    else if (picked.isEmpty()) result = QLatin1String("CANCELLED (the picker opened)");
    else result = picked;

    // Also left on disk, so the outcome survives dismissing the note.
    const QString dir = QLatin1String("C:/Data/Symbigram");
    QDir().mkpath(dir);
    QFile f(dir + QLatin1String("/pick-") + QLatin1String(PROBE_TAG) + QLatin1String(".txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << result << endl;
        f.close();
    }

    QMessageBox::information(0, QLatin1String("Probe " PROBE_TAG), result);
    return 0;
}
