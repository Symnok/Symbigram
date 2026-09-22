// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Writes TL (Type Language) binary, the encoding MTProto carries every request and
// response in. Everything is little-endian and 4-byte aligned. The one irregular case is
// the byte string, which uses a short or long length prefix and then pads to the next
// 4-byte boundary - get that padding wrong and the server rejects the whole message with no
// useful diagnostic, so it is centralised here.
#ifndef TLWRITER_H
#define TLWRITER_H

#include <QByteArray>
#include <QList>
#include <QString>

class TlWriter
{
public:
    explicit TlWriter(int capacity = 256) { m_buf.reserve(capacity); }

    int length() const { return m_buf.size(); }
    QByteArray toByteArray() const { return m_buf; }

    TlWriter &writeByte(quint8 v) { m_buf.append(char(v)); return *this; }
    TlWriter &writeInt(qint32 v);
    TlWriter &writeUInt(quint32 v) { return writeInt(qint32(v)); }
    /// Constructor ids are written exactly like an int; named for readability.
    TlWriter &writeConstructor(quint32 id) { return writeUInt(id); }
    TlWriter &writeLong(qint64 v);
    TlWriter &writeDouble(double v);
    /// Raw bytes with no length prefix and no padding - int128, int256, key material,
    /// or an already serialised object.
    TlWriter &writeRaw(const QByteArray &data) { m_buf.append(data); return *this; }
    /// TL byte string: lengths below 254 use a single length byte; longer ones use 0xFE
    /// followed by a 3-byte length. Either way the total is padded with zeros to a
    /// multiple of 4.
    TlWriter &writeBytes(const QByteArray &data);
    TlWriter &writeString(const QString &s) { return writeBytes(s.toUtf8()); }
    TlWriter &writeBool(bool v);
    TlWriter &writeVectorOfLong(const QList<qint64> &items);
    TlWriter &writeVectorOfInt(const QList<qint32> &items);

private:
    QByteArray m_buf;
};

#endif // TLWRITER_H
