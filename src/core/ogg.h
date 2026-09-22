// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A small Ogg bitstream reader and writer, just enough for Opus-in-Ogg (RFC 7845) voice
// messages: one logical stream, packets reassembled across the lacing table and page
// boundaries. Not a general Ogg implementation - no multiplexing, no seeking.
#ifndef OGG_H
#define OGG_H

#include <QByteArray>
#include <QList>

class OggReader
{
public:
    explicit OggReader(const QByteArray &data);

    /// Pulls the next packet of the first logical stream, with the granule position of the
    /// page it finished on (-1 when the packet does not end a page). Returns false at the end.
    bool nextPacket(QByteArray &packet, qint64 &granulePos);

    bool ok() const { return m_ok; }

private:
    bool readPage();
    QByteArray m_data;
    int m_pos;
    qint32 m_serial;          // the logical stream we follow (the first one seen)
    bool m_haveSerial;
    bool m_ok;
    // Packets buffered from pages already parsed, with the granule of a page-ending packet.
    QList<QByteArray> m_packets;
    QList<qint64> m_granules;
    QByteArray m_partial;     // a packet still being assembled across pages
};

class OggWriter
{
public:
    explicit OggWriter(qint32 serial);

    /// Appends one packet. `granulePos` is written on the page a packet ends; `flush` forces
    /// a page boundary after this packet (used for the two Opus headers). `eos` marks the end.
    void addPacket(const QByteArray &packet, qint64 granulePos, bool flush, bool eos);
    QByteArray result();

private:
    void emitPage(const QByteArray &body, const QByteArray &lacing, qint64 granule, bool bos, bool eos, bool continued);
    qint32 m_serial;
    quint32 m_page;
    QByteArray m_out;
    bool m_bosDone;
};

/// CRC-32 with Ogg's polynomial (0x04c11db7, no reflection). Public for the writer and tests.
quint32 oggCrc(const QByteArray &page);

#endif // OGG_H
