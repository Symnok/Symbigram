// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "ogg.h"

namespace
{
    // Ogg's CRC-32: polynomial 0x04c11db7, no input/output reflection, zero init.
    struct CrcTable
    {
        quint32 t[256];
        CrcTable()
        {
            for (quint32 i = 0; i < 256; ++i) {
                quint32 r = i << 24;
                for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04c11db7u : (r << 1);
                t[i] = r;
            }
        }
    };
    const CrcTable &crcTable() { static CrcTable c; return c; }

    quint32 rd32(const uchar *p) { return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24); }
    void wr32(QByteArray &b, quint32 v) { b.append(char(v)).append(char(v >> 8)).append(char(v >> 16)).append(char(v >> 24)); }
    void wr64(QByteArray &b, qint64 v) { for (int i = 0; i < 8; ++i) b.append(char(quint64(v) >> (8 * i))); }
}

quint32 oggCrc(const QByteArray &page)
{
    const uchar *p = reinterpret_cast<const uchar *>(page.constData());
    const CrcTable &tbl = crcTable();
    quint32 crc = 0;
    for (int i = 0; i < page.size(); ++i) crc = (crc << 8) ^ tbl.t[((crc >> 24) ^ p[i]) & 0xff];
    return crc;
}

// -- reader --------------------------------------------------------------------------------------------

OggReader::OggReader(const QByteArray &data)
    : m_data(data), m_pos(0), m_serial(0), m_haveSerial(false), m_ok(true)
{
}

bool OggReader::readPage()
{
    // Find the next "OggS" capture pattern.
    const uchar *d = reinterpret_cast<const uchar *>(m_data.constData());
    const int n = m_data.size();
    while (m_pos + 27 <= n) {
        if (!(d[m_pos] == 'O' && d[m_pos + 1] == 'g' && d[m_pos + 2] == 'g' && d[m_pos + 3] == 'S')) { ++m_pos; continue; }
        const int hdr = m_pos;
        const int htype = d[hdr + 5];
        const qint64 granule = qint64(quint64(rd32(d + hdr + 6)) | (quint64(rd32(d + hdr + 10)) << 32));
        const qint32 serial = qint32(rd32(d + hdr + 14));
        const int nsegs = d[hdr + 26];
        if (hdr + 27 + nsegs > n) { m_ok = false; return false; }
        const uchar *segs = d + hdr + 27;
        int bodyLen = 0;
        for (int i = 0; i < nsegs; ++i) bodyLen += segs[i];
        const int bodyStart = hdr + 27 + nsegs;
        if (bodyStart + bodyLen > n) { m_ok = false; return false; }
        m_pos = bodyStart + bodyLen;    // advance past this page regardless

        if (!m_haveSerial) { m_serial = serial; m_haveSerial = true; }
        if (serial != m_serial) continue;   // a different logical stream: skip

        // A page that does not continue our partial packet but we have one buffered means a
        // gap; drop the stale partial rather than corrupt the next packet.
        if (!(htype & 0x01)) m_partial.clear();

        int off = bodyStart;
        int lastEnd = -1;                    // index into m_packets of the last packet ending here
        for (int i = 0; i < nsegs; ++i) {
            const int len = segs[i];
            m_partial.append(m_data.constData() + off, len);
            off += len;
            if (len < 255) {                 // this lacing value ends a packet
                m_packets.append(m_partial);
                m_granules.append(-1);
                lastEnd = m_packets.size() - 1;
                m_partial.clear();
            }
        }
        if (lastEnd >= 0) m_granules[lastEnd] = granule;
        return true;
    }
    return false;
}

bool OggReader::nextPacket(QByteArray &packet, qint64 &granulePos)
{
    while (m_packets.isEmpty()) {
        if (!readPage()) return false;
    }
    packet = m_packets.takeFirst();
    granulePos = m_granules.takeFirst();
    return true;
}

// -- writer --------------------------------------------------------------------------------------------

OggWriter::OggWriter(qint32 serial)
    : m_serial(serial), m_page(0), m_bosDone(false)
{
}

void OggWriter::emitPage(const QByteArray &body, const QByteArray &lacing, qint64 granule, bool bos, bool eos, bool continued)
{
    QByteArray page;
    page.append("OggS", 4);
    page.append(char(0));                    // stream structure version
    page.append(char((continued ? 0x01 : 0) | (bos ? 0x02 : 0) | (eos ? 0x04 : 0)));
    wr64(page, granule);
    wr32(page, quint32(m_serial));
    wr32(page, m_page++);
    const int crcPos = page.size();
    wr32(page, 0);                           // checksum placeholder
    page.append(char(lacing.size()));
    page.append(lacing);
    page.append(body);
    const quint32 crc = oggCrc(page);
    page[crcPos] = char(crc);
    page[crcPos + 1] = char(crc >> 8);
    page[crcPos + 2] = char(crc >> 16);
    page[crcPos + 3] = char(crc >> 24);
    m_out.append(page);
}

void OggWriter::addPacket(const QByteArray &packet, qint64 granulePos, bool flush, bool eos)
{
    Q_UNUSED(flush);                         // one page per packet: always a boundary
    QByteArray lacing;
    int remaining = packet.size();
    while (remaining >= 255) { lacing.append(char(255)); remaining -= 255; }
    lacing.append(char(remaining));          // a value 0..254 terminates the packet (0 if a multiple of 255)
    emitPage(packet, lacing, granulePos, !m_bosDone, eos, false);
    m_bosDone = true;
}

QByteArray OggWriter::result() { return m_out; }
