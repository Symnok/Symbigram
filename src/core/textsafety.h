// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Guards against text that is valid Unicode but ruinous to render here.
//
// A display name can carry unlimited combining marks - "Zalgo" text. One seen in the wild had six
// visible characters wearing 58 marks, 28 of them on a single space. Symbian rasterises glyphs in
// the shared font and bitmap server, so composing a cluster like that does not merely slow the
// application down: it can take the whole phone with it, which is exactly what happened on a
// Nokia 700 (a faster 808 survived the same message).
#ifndef TEXTSAFETY_H
#define TEXTSAFETY_H

#include <QString>

namespace TextSafety
{
    /// Caps how many combining marks may ride on one base character; the rest are dropped.
    /// Two is enough for every real language this app will meet - Vietnamese stacks at most two,
    /// and Hebrew points are separate code points, not marks.
    QString limitCombiningMarks(const QString &s, int maxPerBase = 2);
}

#endif // TEXTSAFETY_H
