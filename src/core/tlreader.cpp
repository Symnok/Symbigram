// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "tlreader.h"
#include "tlconstructors.h"

#include <cstring>

void TlReader::need(int count) const
{
    if (count < 0 || m_pos + count > m_buf.size())
        throw TlException(QString::fromLatin1("TL read past end: wanted %1 at %2 of %3").arg(count).arg(m_pos).arg(m_buf.size()));
}

quint8 TlReader::readByte()
{
    need(1);
    return quint8(m_buf.at(m_pos++));
}

qint32 TlReader::readInt()
{
    need(4);
    const uchar *b = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos;
    qint32 v = qint32(quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24));
    m_pos += 4;
    return v;
}

quint32 TlReader::peekConstructor()
{
    int saved = m_pos;
    quint32 c = readUInt();
    m_pos = saved;
    return c;
}

qint64 TlReader::readLong()
{
    need(8);
    const uchar *b = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos;
    quint64 v = 0;
    for (int i = 0; i < 8; ++i) v |= quint64(b[i]) << (8 * i);
    m_pos += 8;
    return qint64(v);
}

double TlReader::readDouble()
{
    qint64 bits = readLong();
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

QByteArray TlReader::readRaw(int count)
{
    need(count);
    QByteArray r = m_buf.mid(m_pos, count);
    m_pos += count;
    return r;
}

QByteArray TlReader::readBytes()
{
    const int start = m_pos;
    int len = readByte();
    if (len == 0xFE) {
        need(3);
        const uchar *b = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos;
        len = int(b[0]) | (int(b[1]) << 8) | (int(b[2]) << 16);
        m_pos += 3;
    } else if (len > 0xFE) {
        throw TlException(QString::fromLatin1("invalid TL length prefix 0x%1").arg(len, 2, 16, QLatin1Char('0')));
    }
    QByteArray data = readRaw(len);
    const int pad = (4 - ((m_pos - start) & 3)) & 3;
    need(pad);
    m_pos += pad;
    return data;
}

QString TlReader::readString()
{
    return QString::fromUtf8(readBytes());
}

bool TlReader::readBool()
{
    quint32 c = readConstructor();
    if (c == Tl::BoolTrue) return true;
    if (c == Tl::BoolFalse) return false;
    throw TlException(QString::fromLatin1("expected Bool, got 0x%1").arg(c, 8, 16, QLatin1Char('0')));
}

QList<qint64> TlReader::readVectorOfLong()
{
    quint32 c = readConstructor();
    if (c != Tl::Vector)
        throw TlException(QString::fromLatin1("expected vector, got 0x%1").arg(c, 8, 16, QLatin1Char('0')));
    int count = readInt();
    if (count < 0 || count > remaining() / 8)
        throw TlException(QString::fromLatin1("implausible vector count %1").arg(count));
    QList<qint64> r;
    for (int i = 0; i < count; ++i) r.append(readLong());
    return r;
}

void TlReader::expect(quint32 constructor, const char *what)
{
    quint32 c = readConstructor();
    if (c != constructor)
        throw TlException(QString::fromLatin1("expected %1 (0x%2), got 0x%3").arg(QLatin1String(what))
                          .arg(constructor, 8, 16, QLatin1Char('0')).arg(c, 8, 16, QLatin1Char('0')));
}
