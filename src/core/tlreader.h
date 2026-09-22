// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Reads TL binary. Mirrors TlWriter, including the byte-string padding rules. Every read
// is bounds-checked and throws TlException rather than returning junk: the data arrives
// from the network before it has been authenticated, so a malformed length must fail
// loudly instead of walking off the end of the buffer.
#ifndef TLREADER_H
#define TLREADER_H

#include <QByteArray>
#include <QList>
#include <QString>

/// Thrown by the TL and MTProto layers. Not derived from std::exception on purpose: the
/// Symbian STL is a separate library and a plain class is the same on every toolchain.
class TlException
{
public:
    explicit TlException(const QString &message) : m_message(message) {}
    QString message() const { return m_message; }
private:
    QString m_message;
};

class TlReader
{
public:
    explicit TlReader(const QByteArray &data, int offset = 0) : m_buf(data), m_pos(offset) {}

    int position() const { return m_pos; }
    void setPosition(int pos) { m_pos = pos; }
    int remaining() const { return m_buf.size() - m_pos; }
    bool atEnd() const { return m_pos >= m_buf.size(); }

    quint8 readByte();
    qint32 readInt();
    quint32 readUInt() { return quint32(readInt()); }
    quint32 readConstructor() { return readUInt(); }
    /// Peeks at the next constructor without consuming it.
    quint32 peekConstructor();
    qint64 readLong();
    double readDouble();
    QByteArray readRaw(int count);
    QByteArray readBytes();
    QString readString();
    bool readBool();
    QList<qint64> readVectorOfLong();
    /// Reads a constructor and throws unless it is the expected one.
    void expect(quint32 constructor, const char *what);

private:
    void need(int count) const;
    QByteArray m_buf;
    int m_pos;
};

#endif // TLREADER_H
