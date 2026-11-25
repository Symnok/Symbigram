// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "emojitext.h"
#include "emojitable.h"

#include <QChar>
#include <QLatin1String>

namespace
{
    /// The table is sorted by code point, so this is a plain binary search. Two thousand entries
    /// is far too many to walk per character of every message on a 680 MHz phone.
    const EmojiTable::Entry *find(uint cp)
    {
        int lo = 0;
        int hi = EmojiTable::KCount - 1;
        while (lo <= hi) {
            const int mid = lo + (hi - lo) / 2;
            const uint at = EmojiTable::KEntries[mid].cp;
            if (at == cp) return &EmojiTable::KEntries[mid];
            if (at < cp) lo = mid + 1;
            else hi = mid - 1;
        }
        return 0;
    }

    bool isRegionalIndicator(uint cp)
    {
        return cp >= 0x1F1E6 && cp <= 0x1F1FF;
    }
}

namespace EmojiText
{

bool isModifier(uint cp)
{
    return (cp >= 0xFE00 && cp <= 0xFE0F)       // variation selectors (FE0F = "draw as emoji")
        || (cp >= 0x1F3FB && cp <= 0x1F3FF)     // skin tones
        || cp == 0x200D                         // zero-width joiner
        || cp == 0x200B                         // zero-width space, common in pasted emoji art
        || cp == 0x20E3;                        // combining enclosing keycap
}

bool isEmoji(uint cp)
{
    if (isModifier(cp)) return true;

    // The big blocks.
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return true;     // pictographs, emoticons, transport, ...
    if (cp >= 0x1FB00 && cp <= 0x1FBFF) return true;     // symbols for legacy computing
    if (cp >= 0x2600 && cp <= 0x27BF) return true;       // misc symbols + dingbats
    if (cp >= 0x2B00 && cp <= 0x2BFF) return true;       // arrows and stars used as emoji

    // The stragglers. These sit outside every range above, so before this list they were the one
    // kind of emoji that reached the font - and drew a box. They are not contiguous and there is
    // no pattern to them; this is simply the set Unicode gives Emoji_Presentation to.
    switch (cp) {
    case 0x00A9: case 0x00AE:                            // (c) (r)
    case 0x203C: case 0x2049:                            // !! !?
    case 0x2122: case 0x2139:                            // (tm) info
    case 0x21A9: case 0x21AA:                            // hooked arrows
    case 0x231A: case 0x231B:                            // watch, hourglass
    case 0x2328: case 0x23CF:                            // keyboard, eject
    case 0x24C2:                                         // circled M
    case 0x25AA: case 0x25AB: case 0x25B6: case 0x25C0:  // small squares, play, reverse
    case 0x2934: case 0x2935:                            // curving arrows
    case 0x3030: case 0x303D:                            // wavy dash, part alternation
    case 0x3297: case 0x3299:                            // circled ideographs
        return true;
    default:
        break;
    }
    if (cp >= 0x2194 && cp <= 0x2199) return true;       // arrows
    if (cp >= 0x23E9 && cp <= 0x23F3) return true;       // media controls, timers
    if (cp >= 0x23F8 && cp <= 0x23FA) return true;       // pause, stop, record
    if (cp >= 0x25FB && cp <= 0x25FE) return true;       // medium squares
    return false;
}

bool hasArtwork(uint cp)
{
    const EmojiTable::Entry *e = find(cp);
    return e && e->art;
}

QString artworkUrl(uint cp)
{
    if (!hasArtwork(cp)) return QString();
    // Lower-case hex, padded to four digits for the code points below U+1000 - exactly how
    // gen-emoji.py's "e_%04x.png" names the files. Without the padding U+00A9 would ask for
    // e_a9.png and silently get nothing.
    return QLatin1String("qrc:/emoji/e_")
         + QString::number(cp, 16).rightJustified(4, QLatin1Char('0'))
         + QLatin1String(".png");
}

QString name(uint cp)
{
    const EmojiTable::Entry *e = find(cp);
    return e ? QString::fromLatin1(e->name) : QString();
}

QString shortcodeFor(uint cp)
{
    const EmojiTable::Entry *e = find(cp);
    if (!e || !e->art) return QString();
    QString n = QString::fromLatin1(e->name);
    n.replace(QLatin1Char(' '), QLatin1Char('_'));
    return QLatin1Char(':') + n + QLatin1Char(':');
}

QString fromShortcodes(const QString &text)
{
    if (!text.contains(QLatin1Char(':'))) return text;
    QString out;
    int pos = 0;
    while (pos < text.size()) {
        const int open = text.indexOf(QLatin1Char(':'), pos);
        if (open < 0) break;
        const int close = text.indexOf(QLatin1Char(':'), open + 1);
        if (close < 0) break;
        QString inner = text.mid(open + 1, close - open - 1);
        inner.replace(QLatin1Char('_'), QLatin1Char(' '));

        // Linear over the table, but only for text that actually holds a pair of colons, and a
        // message carries a handful of emoji at most.
        uint found = 0;
        if (!inner.isEmpty()) {
            for (int i = 0; i < EmojiTable::KCount; ++i) {
                if (!EmojiTable::KEntries[i].art) continue;
                if (inner == QLatin1String(EmojiTable::KEntries[i].name)) {
                    found = EmojiTable::KEntries[i].cp;
                    break;
                }
            }
        }
        if (!found) {
            // Not one of ours. Advance past the OPENING colon only, so that in 10:30:00 the
            // second colon still gets its chance to open a match rather than being skipped.
            out += text.mid(pos, open + 1 - pos);
            pos = open + 1;
            continue;
        }
        out += text.mid(pos, open - pos);
        if (found > 0xFFFF) {
            out += QChar(QChar::highSurrogate(found));
            out += QChar(QChar::lowSurrogate(found));
        } else {
            out += QChar(ushort(found));
        }
        pos = close + 1;
    }
    out += text.mid(pos);
    return out;
}

uint codePointAt(const QString &text, int &i)
{
    const QChar c = text.at(i);
    if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
        const uint cp = QChar::surrogateToUcs4(c, text.at(i + 1));
        i += 2;
        return cp;
    }
    ++i;
    return c.unicode();
}

bool isOnlyEmoji(const QString &text)
{
    bool any = false;
    int i = 0;
    while (i < text.size()) {
        if (text.at(i).isSpace()) { ++i; continue; }
        const uint cp = codePointAt(text, i);
        if (!isEmoji(cp)) return false;
        if (!isModifier(cp)) any = true;     // a lone skin tone is not a message
    }
    return any;
}

QString toNames(const QString &text)
{
    QString out;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c.isSpace()) { out += c; ++i; continue; }
        const int was = i;
        const uint cp = codePointAt(text, i);
        if (isModifier(cp)) continue;                    // folded away, never named
        if (!isEmoji(cp)) { out += text.mid(was, i - was); continue; }

        if (isRegionalIndicator(cp)) {
            // A flag, as a pair of regional indicators. Named generically and on purpose: WHICH
            // flag is exactly what this client does not say, for the same reason there is no flag
            // artwork and the Hebrew translation is a separate add-on. The second indicator of
            // the pair is swallowed so one flag reads as one word.
            if (i < text.size()) {
                const int peek = i;
                int j = i;
                const uint next = codePointAt(text, j);
                if (isRegionalIndicator(next)) i = j;
                else i = peek;
            }
            out += QLatin1String("[flag]");
            continue;
        }

        const QString n = name(cp);
        out += QLatin1Char('[');
        out += n.isEmpty() ? QString::fromLatin1("emoji") : n;
        out += QLatin1Char(']');
    }
    return out;
}

namespace {

/// Which band of the picker a code point belongs in; lower comes first.
int paletteBand(uint cp)
{
    // The Emoticons block - the smileys themselves, and the three monkeys that get used as
    // faces at the end of it.
    if (cp >= 0x1F600 && cp <= 0x1F64A) return 0;
    if (cp == 0x1F91D) return 1;                     // handshake sits among the later faces
    if (cp >= 0x1F910 && cp <= 0x1F9D0) return 0;    // the faces Unicode added later

    // Hands and gestures.
    if (cp >= 0x270A && cp <= 0x270C) return 1;
    if (cp >= 0x1F44A && cp <= 0x1F450) return 1;
    if (cp == 0x1F4AA || cp == 0x1F64C || cp == 0x1F64F) return 1;

    // Hearts, and the kiss mark that goes with them.
    if (cp >= 0x1F493 && cp <= 0x1F49C) return 2;
    if (cp == 0x2764 || cp == 0x1F48B || cp == 0x1F5A4 || cp == 0x1F90D || cp == 0x1F9E1) return 2;

    return 3;                                        // symbols, stars, fire, presents, the rest
}

}

QList<uint> paletteOrder()
{
    // Four passes over a table that is already in code-point order, rather than a sort: this
    // keeps the Unicode order inside each band for free and needs no comparator.
    QList<uint> out;
    for (int band = 0; band < 4; ++band)
        for (int i = 0; i < EmojiTable::KCount; ++i)
            if (EmojiTable::KEntries[i].art && paletteBand(EmojiTable::KEntries[i].cp) == band)
                out.append(EmojiTable::KEntries[i].cp);
    return out;
}

}
