# Symbigram - a Telegram client for Symbian Anna/Belle.
# Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#
# Generates the bundled emoji artwork and the lookup table that goes with it.
#
# The phone font has no emoji glyphs at all, so an emoji in a message renders as an empty box -
# everything shown has to be a bundled PNG. That puts artwork on a budget, so there are two tiers:
#
#   BASIC    - gets a colour PNG (~750 bytes each). Deliberately small: the faces, hands, hearts
#              and symbols that make up the great majority of what people actually send.
#   EXTENDED - gets a NAME but no artwork. Names are strings, perhaps 25 bytes each, so a wide
#              set is nearly free. A message that is nothing but emoji falls back to "[thumbs up]"
#              rather than arriving with an empty body, which is what it used to do.
#
# Anything in neither tier is dropped from the text, as every emoji was before this existed.
#
# Deliberately NOT included at either tier:
#   * flag sequences (regional indicator pairs). Hundreds of them, and a client that renders a
#     particular flag on a phone can be a safety problem for its owner - the same reason the
#     Hebrew translation ships as a separate add-on rather than inside the binary.
#   * skin-tone modifiers and ZWJ sequences (families, professions). The renderer folds a tone
#     modifier away to the base emoji and drops an unmatched ZWJ sequence.
#
# ARTWORK SOURCE - Google's Noto Emoji, Apache-2.0, the same source and policy as the QipIM /
# KeepIM X-status packs. No Microsoft, ICQ, QIP or Mail.ru artwork is used anywhere.
#
#   font: NotoColorEmoji-noflags.ttf  (9,891,544 bytes)
#   from: https://raw.githubusercontent.com/googlefonts/noto-emoji/main/2D/fonts/
#   kept: d:/ver/noto-emoji/  (NOT in the repository - it is 9.4 MB and only needed to re-render)
#
# The "-noflags" build is deliberate: flag emoji are absent from the font itself, so they cannot
# be rendered by accident rather than merely being left off a list here. See the note above about
# why a client that draws a particular flag can be a problem for the person holding the phone.
#
# NotoColorEmoji is a CBDT bitmap font with exactly ONE strike, at pixel size 109 - any other
# size raises "invalid pixel size" - so everything is rendered at 109 and scaled down. Rendering
# from the font rather than fetching the repository's 2D/png/128/ PNGs was measured, not assumed:
# across a 16-emoji sample the two routes differ by 0.53 of 255 per channel, i.e. not at all to
# the eye, and the font makes a change of set or size a local re-run with no network.
#
# Ship licenses/noto-emoji-LICENSE-Apache-2.0.txt with the application and credit Noto in About.
#
# Run from the repository root:  python tools/gen-emoji.py [path-to-font]
# Writes images/emoji/e_<hex>.png, emoji.qrc and src/core/emojitable.h.

import io
import os
import sys
import unicodedata

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

FONT = os.environ.get("SGM_EMOJI_FONT", "d:/ver/noto-emoji/NotoColorEmoji-noflags.ttf")
STRIKE = 109       # the only pixel size NotoColorEmoji's CBDT table offers
SIZE = 24          # bundled at 24px; the renderer scales to the line height of the chosen font
GLYPH = 22         # the glyph's long edge inside the 24px box, leaving a pixel of air each side
OUT_IMAGES = os.path.join("images", "emoji")
OUT_QRC = "emoji.qrc"
OUT_HEADER = os.path.join("src", "core", "emojitable.h")

# --- tier 1: artwork ------------------------------------------------------------------------------

FACES = [
    0x1F600, 0x1F601, 0x1F602, 0x1F603, 0x1F604, 0x1F605, 0x1F606, 0x1F607,
    0x1F608, 0x1F609, 0x1F60A, 0x1F60B, 0x1F60C, 0x1F60D, 0x1F60E, 0x1F60F,
    0x1F610, 0x1F612, 0x1F613, 0x1F614, 0x1F615, 0x1F616, 0x1F618, 0x1F61A,
    0x1F61B, 0x1F61C, 0x1F61D, 0x1F61E, 0x1F61F, 0x1F620, 0x1F621, 0x1F622,
    0x1F623, 0x1F624, 0x1F625, 0x1F626, 0x1F627, 0x1F628, 0x1F629, 0x1F62A,
    0x1F62B, 0x1F62C, 0x1F62D, 0x1F62E, 0x1F62F, 0x1F630, 0x1F631, 0x1F632,
    0x1F633, 0x1F634, 0x1F635, 0x1F636, 0x1F637, 0x1F641, 0x1F642, 0x1F643,
    0x1F644, 0x1F910, 0x1F911, 0x1F912, 0x1F913, 0x1F914, 0x1F915, 0x1F917,
    0x1F923, 0x1F924, 0x1F925, 0x1F926, 0x1F929, 0x1F92A, 0x1F92B, 0x1F92D,
    0x1F92F, 0x1F970, 0x1F973, 0x1F97A, 0x1F9D0,
    0x1F648, 0x1F649, 0x1F64A,
]

HANDS = [
    0x1F44A, 0x1F44B, 0x1F44C, 0x1F44D, 0x1F44E, 0x1F44F, 0x1F450, 0x1F4AA,
    0x1F91D, 0x1F64C, 0x1F64F, 0x270A, 0x270B, 0x270C,
]

HEARTS = [
    0x2764, 0x1F493, 0x1F494, 0x1F495, 0x1F496, 0x1F499, 0x1F49A, 0x1F49B,
    0x1F49C, 0x1F5A4, 0x1F90D, 0x1F9E1, 0x1F48B,
]

CORE_SYMBOLS = [
    0x2B50, 0x2728, 0x1F525, 0x1F4A5, 0x1F4A9, 0x1F4AF, 0x2705, 0x274C,
    0x2757, 0x2753, 0x1F389, 0x1F38A, 0x1F381, 0x1F3C6, 0x1F680, 0x26A1,
    0x1F44D, 0x1F4A4, 0x1F4AC, 0x1F514,
]

BASIC = FACES + HANDS + HEARTS + CORE_SYMBOLS

# --- tier 2: names only --------------------------------------------------------------------------
# Generous, because a name costs a couple of dozen bytes. Includes the ranges the old stripper
# missed (U+203C, U+2049, U+2139, U+2194..2199, U+231A..231B, U+23E9..23F3, U+25AA..25FE,
# U+2934..2935, U+3297, U+3299 and friends), which have no glyph in the phone font either.

EXTENDED = []
EXTENDED += list(range(0x1F600, 0x1F650))          # the rest of the emoticons block
EXTENDED += list(range(0x1F300, 0x1F400))          # misc symbols and pictographs
EXTENDED += list(range(0x1F400, 0x1F4FF))          # animals, food, objects
EXTENDED += list(range(0x1F500, 0x1F5FF))          # more symbols
EXTENDED += list(range(0x1F680, 0x1F700))          # transport
EXTENDED += list(range(0x1F900, 0x1FA00))          # supplemental
EXTENDED += list(range(0x1FA70, 0x1FB00))          # extended-A
EXTENDED += list(range(0x2600, 0x27C0))            # misc symbols + dingbats
EXTENDED += list(range(0x2B00, 0x2C00))
EXTENDED += [
    0x00A9, 0x00AE, 0x203C, 0x2049, 0x2122, 0x2139,
    0x2194, 0x2195, 0x2196, 0x2197, 0x2198, 0x2199, 0x21A9, 0x21AA,
    0x231A, 0x231B, 0x2328, 0x23CF,
    0x24C2, 0x25AA, 0x25AB, 0x25B6, 0x25C0,
    0x2934, 0x2935, 0x3030, 0x303D, 0x3297, 0x3299,
]
EXTENDED += list(range(0x23E9, 0x23F4))
EXTENDED += list(range(0x23F8, 0x23FB))
EXTENDED += list(range(0x25FB, 0x25FF))

# Regional indicators would combine into flags; excluded on purpose (see the header comment).
FLAGS = set(range(0x1F1E6, 0x1F200))
# Skin tones and ZWJ joiners are folded away by the renderer, never looked up.
MODIFIERS = set(range(0x1F3FB, 0x1F400)) | {0x200D, 0x20E3} | set(range(0xFE00, 0xFE10))


def short_name(cp):
    try:
        n = unicodedata.name(chr(cp))
    except ValueError:
        return None
    n = n.lower()
    # Unicode's formal names carry noise that reads badly in a message bubble. "black" and
    # "white" are NOT stripped, however tempting: they are the only thing separating BLACK HEART
    # from HEAVY BLACK HEART, and collapsing them gave both the name "heart", so one of the two
    # could never be turned back from its shortcode. The round-trip check in the selftest is
    # what caught that.
    for a in ("heavy black ", "heavy "):
        if n.startswith(a):
            n = n[len(a):]
            break
    for a in (" sign", " symbol", " source"):
        if n.endswith(a):
            n = n[: -len(a)]
            break
    return n.strip() or None


def render(cp, font):
    """One emoji as a SIZE x SIZE RGBA tile, or None if the font has no colour glyph for it.

    The glyph is cropped to what was actually painted and then scaled, rather than scaling the
    whole 109px canvas: the canvas carries the font's own metrics padding, which differs between
    glyphs and would land each emoji at a slightly different visual weight."""
    big = Image.new("RGBA", (STRIKE * 2, STRIKE * 2), (0, 0, 0, 0))
    ImageDraw.Draw(big).text((STRIKE, STRIKE), chr(cp), font=font, anchor="mm",
                             embedded_color=True)
    bb = big.getbbox()
    if not bb:
        return None
    g = big.crop(bb)
    w, h = g.size
    if w >= h:
        nw, nh = GLYPH, max(1, int(round(h * GLYPH / float(w))))
    else:
        nh, nw = GLYPH, max(1, int(round(w * GLYPH / float(h))))
    g = g.resize((nw, nh), Image.LANCZOS)
    tile = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    tile.paste(g, ((SIZE - nw) // 2, (SIZE - nh) // 2), g)
    return tile


def main():
    if not os.path.isdir("src"):
        sys.exit("run this from the repository root")
    font_path = sys.argv[1] if len(sys.argv) > 1 else FONT
    if not os.path.exists(font_path):
        sys.exit("Noto colour emoji font not found: %s\n"
                 "Download NotoColorEmoji-noflags.ttf from\n"
                 "  https://raw.githubusercontent.com/googlefonts/noto-emoji/main/2D/fonts/\n"
                 "and put it there, or pass its path as the first argument." % font_path)

    try:
        font = ImageFont.truetype(font_path, STRIKE)
    except OSError as e:
        sys.exit("%s cannot be rendered at %dpx (%s). NotoColorEmoji has a single CBDT strike; "
                 "if this font differs, set STRIKE to its size." % (font_path, STRIKE, e))
    if not os.path.isdir(OUT_IMAGES):
        os.makedirs(OUT_IMAGES)
    for stale in os.listdir(OUT_IMAGES):
        if stale.startswith("e_") and stale.endswith(".png"):
            os.remove(os.path.join(OUT_IMAGES, stale))

    basic = set(BASIC) - FLAGS - MODIFIERS
    entries = {}            # cp -> [name, has_art]

    # Tier 1: render, and keep only what the font actually paints.
    art_bytes = 0
    no_glyph = []
    for cp in sorted(basic):
        # The font silently draws nothing for a code point it has no colour glyph for. Bundling
        # that would leave a blank gap in the message, so it is only kept if something was painted.
        im = render(cp, font)
        if im is None:
            no_glyph.append(cp)
            continue
        # Posterise the colour channels to 5 bits, leaving ALPHA untouched: zlib compresses the
        # flatter gradients appreciably better, while the anti-aliased edges that keep an emoji
        # from looking cut out against a coloured bubble survive exactly.
        px = im.load()
        mask = 0xF8
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b, a = px[x, y]
                px[x, y] = (r & mask, g & mask, b & mask, a)
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        data = buf.getvalue()
        with open(os.path.join(OUT_IMAGES, "e_%04x.png" % cp), "wb") as f:
            f.write(data)
        art_bytes += len(data)
        entries[cp] = [short_name(cp) or "emoji", True]

    # Tier 2: names for everything else that is a real, named code point.
    for cp in EXTENDED:
        if cp in entries or cp in FLAGS or cp in MODIFIERS:
            continue
        n = short_name(cp)
        if n:
            entries[cp] = [n, False]

    keys = sorted(entries)

    # Every name that becomes a shortcode has to be unique, or fromShortcodes() resolves one of
    # them to the wrong code point. Names used only for display may repeat harmlessly.
    taken = {}
    clashes = []
    for cp in keys:
        if not entries[cp][1]:
            continue
        n = entries[cp][0]
        if n in taken:
            clashes.append((n, taken[n], cp))
            i = 2
            while "%s %d" % (n, i) in taken:
                i += 1
            n = "%s %d" % (n, i)
            entries[cp][0] = n
        taken[n] = cp

    name_bytes = sum(len(entries[c][0]) + 1 for c in keys)

    with io.open(OUT_QRC, "w", encoding="utf-8", newline="\n") as f:
        f.write("<!-- Generated by tools/gen-emoji.py - do not edit by hand. -->\n")
        f.write("<RCC>\n    <qresource prefix=\"/emoji\">\n")
        for cp in keys:
            if entries[cp][1]:
                f.write("        <file alias=\"e_%04x.png\">images/emoji/e_%04x.png</file>\n"
                        % (cp, cp))
        f.write("    </qresource>\n</RCC>\n")

    with io.open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Symbigram - a Telegram client for Symbian Anna/Belle.\n")
        f.write("// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.\n")
        f.write("//\n")
        f.write("// Generated by tools/gen-emoji.py - do not edit by hand.\n")
        f.write("//\n")
        f.write("// Every emoji Symbigram knows about, sorted by code point for a binary search.\n")
        f.write("// `art` means there is a bundled PNG at qrc:/emoji/e_<hex>.png to draw it with;\n")
        f.write("// the rest carry only a name, used when a message is nothing but emoji and would\n")
        f.write("// otherwise arrive with an empty body. A code point in neither tier is dropped,\n")
        f.write("// because the phone font has no glyph for it and would show an empty box.\n")
        f.write("#ifndef EMOJITABLE_H\n#define EMOJITABLE_H\n\n")
        f.write("namespace EmojiTable\n{\n")
        f.write("    /// Artwork is bundled at this pixel size; the renderer scales it to the text.\n")
        f.write("    const int KArtworkSize = %d;\n\n" % SIZE)
        f.write("    struct Entry\n    {\n")
        f.write("        unsigned int cp;\n        const char *name;\n        bool art;\n    };\n\n")
        f.write("    const Entry KEntries[] = {\n")
        for cp in keys:
            n, art = entries[cp]
            f.write("        { 0x%05X, \"%s\", %s },\n" % (cp, n, "true" if art else "false"))
        f.write("    };\n")
        f.write("    const int KCount = %d;\n" % len(keys))
        f.write("}\n\n#endif\n")

    art = sum(1 for c in keys if entries[c][1])
    print("artwork: %d emoji, %d KB (avg %d B)" % (art, art_bytes // 1024, art_bytes // max(1, art)))
    print("names:   %d emoji, %d KB of strings" % (len(keys), name_bytes // 1024))
    for n, first, second in clashes:
        print("WARNING: %r was the name of both U+%04X and U+%04X; the second was renamed %r. "
              "Prefer fixing short_name() so both read well."
              % (n, first, second, entries[second][0]))
    if no_glyph:
        print("no colour glyph in the font, no artwork for %d: %s"
              % (len(no_glyph), " ".join("U+%04X" % c for c in no_glyph)))


if __name__ == "__main__":
    main()
