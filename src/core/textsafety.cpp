// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "textsafety.h"

QString TextSafety::limitCombiningMarks(const QString &s, int maxPerBase)
{
    // Cheap pre-check: the overwhelming majority of names and messages have no marks at all,
    // and this runs on everything the app displays.
    bool any = false;
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i).category() == QChar::Mark_NonSpacing) { any = true; break; }
    }
    if (!any) return s;

    QString out;
    out.reserve(s.size());
    int onThisBase = 0;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const QChar::Category cat = c.category();
        const bool mark = (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
                           || cat == QChar::Mark_Enclosing);
        if (!mark) {
            onThisBase = 0;
            out += c;
            continue;
        }
        if (onThisBase < maxPerBase) {
            ++onThisBase;
            out += c;
        }
        // Beyond the cap the mark is dropped: the name still reads, and the font server is not
        // asked to stack something it cannot cope with.
    }
    return out;
}
