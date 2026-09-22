// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "tlwriter.h"
#include "tlconstructors.h"

#include <cstring>

TlWriter &TlWriter::writeInt(qint32 v)
{
    char b[4];
    b[0] = char(v);
    b[1] = char(v >> 8);
    b[2] = char(v >> 16);
    b[3] = char(v >> 24);
    m_buf.append(b, 4);
    return *this;
}

TlWriter &TlWriter::writeLong(qint64 v)
{
    char b[8];
    for (int i = 0; i < 8; ++i) b[i] = char(quint64(v) >> (8 * i));
    m_buf.append(b, 8);
    return *this;
}

TlWriter &TlWriter::writeDouble(double v)
{
    qint64 bits;
    memcpy(&bits, &v, 8);   // IEEE 754 little-endian on every target this runs on
    return writeLong(bits);
}

TlWriter &TlWriter::writeBytes(const QByteArray &data)
{
    const int start = m_buf.size();
    const int len = data.size();
    if (len <= 253) {
        writeByte(quint8(len));
    } else {
        writeByte(0xFE);
        writeByte(quint8(len));
        writeByte(quint8(len >> 8));
        writeByte(quint8(len >> 16));
    }
    m_buf.append(data);
    const int pad = (4 - ((m_buf.size() - start) & 3)) & 3;
    for (int i = 0; i < pad; ++i) writeByte(0);
    return *this;
}

TlWriter &TlWriter::writeBool(bool v)
{
    return writeConstructor(v ? Tl::BoolTrue : Tl::BoolFalse);
}

TlWriter &TlWriter::writeVectorOfLong(const QList<qint64> &items)
{
    writeConstructor(Tl::Vector);
    writeInt(items.size());
    for (int i = 0; i < items.size(); ++i) writeLong(items.at(i));
    return *this;
}

TlWriter &TlWriter::writeVectorOfInt(const QList<qint32> &items)
{
    writeConstructor(Tl::Vector);
    writeInt(items.size());
    for (int i = 0; i < items.size(); ++i) writeInt(items.at(i));
    return *this;
}
