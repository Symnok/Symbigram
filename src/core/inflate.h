// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// DEFLATE decompression (RFC 1951) and the gzip wrapper around it (RFC 1952). Telegram
// compresses any sizeable response and hands it back as gzip_packed, so this is not
// optional. Qt's qUncompress wants a zlib header that gzip does not carry, hence a small
// inflater of our own (ported from LumigramPlus). Decompression only.
#ifndef INFLATE_H
#define INFLATE_H

#include <QByteArray>

namespace Inflate
{
    /// Strips the gzip header and trailer, then inflates the contents. Throws TlException.
    QByteArray gunzip(const QByteArray &data);
    QByteArray inflateRaw(const QByteArray &data, int offset);
}

#endif // INFLATE_H
