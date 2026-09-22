// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "inflate.h"
#include "tlreader.h"

#include <QVector>

namespace
{
    const int MaxBits = 15;

    const int LengthBase[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    const int LengthExtra[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    const int DistBase[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    const int DistExtra[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    const int CodeLengthOrder[] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

    struct State
    {
        const uchar *data;
        int size;
        int pos;
        quint32 bitBuffer;
        int bitCount;

        State(const QByteArray &d, int offset)
            : data(reinterpret_cast<const uchar *>(d.constData())), size(d.size()), pos(offset), bitBuffer(0), bitCount(0) {}

        int readByte()
        {
            if (pos >= size) throw TlException(QLatin1String("deflate stream ended early"));
            return data[pos++];
        }

        int readBits(int need)
        {
            while (bitCount < need) {
                bitBuffer |= quint32(readByte()) << bitCount;
                bitCount += 8;
            }
            int value = int(bitBuffer & ((1u << need) - 1));
            bitBuffer >>= need;
            bitCount -= need;
            return value;
        }

        void alignToByte() { bitBuffer = 0; bitCount = 0; }
    };

    struct Output
    {
        QByteArray buf;
        void write(uchar b) { buf.append(char(b)); }
        /// Back-reference copy, byte by byte: the run may overlap itself.
        void copy(int distance, int length)
        {
            if (distance > buf.size()) throw TlException(QLatin1String("back-reference points before the start of the output"));
            int from = buf.size() - distance;
            buf.reserve(buf.size() + length);
            for (int i = 0; i < length; ++i) buf.append(buf.at(from + i));
        }
    };

    /// Canonical Huffman table: counts per bit length, plus symbols ordered by (length, symbol).
    struct Huffman
    {
        int count[MaxBits + 1];
        QVector<int> symbol;
    };

    void buildHuffman(Huffman &h, const int *lengths, int n)
    {
        for (int i = 0; i <= MaxBits; ++i) h.count[i] = 0;
        h.symbol.resize(n);
        for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
        h.count[0] = 0;
        int offs[MaxBits + 1];
        offs[0] = 0;
        for (int i = 1; i <= MaxBits; ++i) offs[i] = offs[i - 1] + h.count[i - 1];
        for (int i = 0; i < n; ++i)
            if (lengths[i] != 0) h.symbol[offs[lengths[i]]++] = i;
    }

    int decode(State &s, const Huffman &h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= MaxBits; ++len) {
            code |= s.readBits(1);
            int count = h.count[len];
            if (code - first < count) return h.symbol.at(index + (code - first));
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw TlException(QLatin1String("invalid Huffman code"));
    }

    void stored(State &s, Output &out)
    {
        s.alignToByte();
        int len = s.readByte() | (s.readByte() << 8);
        int nlen = s.readByte() | (s.readByte() << 8);
        if ((len & 0xFFFF) != ((~nlen) & 0xFFFF)) throw TlException(QLatin1String("stored block length check failed"));
        for (int i = 0; i < len; ++i) out.write(uchar(s.readByte()));
    }

    void compressed(State &s, Output &out, const Huffman &lengths, const Huffman &distances)
    {
        while (true) {
            int sym = decode(s, lengths);
            if (sym < 256) {
                out.write(uchar(sym));
            } else if (sym == 256) {
                return;
            } else {
                sym -= 257;
                if (sym >= 29) throw TlException(QLatin1String("invalid length symbol"));
                int len = LengthBase[sym] + s.readBits(LengthExtra[sym]);
                int dsym = decode(s, distances);
                if (dsym >= 30) throw TlException(QLatin1String("invalid distance symbol"));
                int dist = DistBase[dsym] + s.readBits(DistExtra[dsym]);
                out.copy(dist, len);
            }
        }
    }

    void dynamic(State &s, Output &out)
    {
        int hlit = s.readBits(5) + 257;
        int hdist = s.readBits(5) + 1;
        int hclen = s.readBits(4) + 4;

        int clens[19] = { 0 };
        for (int i = 0; i < hclen; ++i) clens[CodeLengthOrder[i]] = s.readBits(3);
        Huffman codeLengths;
        buildHuffman(codeLengths, clens, 19);

        QVector<int> lengths(hlit + hdist, 0);
        int pos = 0;
        while (pos < lengths.size()) {
            int sym = decode(s, codeLengths);
            if (sym < 16) {
                lengths[pos++] = sym;
            } else if (sym == 16) {
                if (pos == 0) throw TlException(QLatin1String("repeat with no previous length"));
                int prev = lengths[pos - 1];
                int repeat = 3 + s.readBits(2);
                while (repeat-- > 0 && pos < lengths.size()) lengths[pos++] = prev;
            } else if (sym == 17) {
                int repeat = 3 + s.readBits(3);
                while (repeat-- > 0 && pos < lengths.size()) lengths[pos++] = 0;
            } else {
                int repeat = 11 + s.readBits(7);
                while (repeat-- > 0 && pos < lengths.size()) lengths[pos++] = 0;
            }
        }

        Huffman lit, dist;
        buildHuffman(lit, lengths.constData(), hlit);
        buildHuffman(dist, lengths.constData() + hlit, hdist);
        compressed(s, out, lit, dist);
    }

    const Huffman &fixedLengths()
    {
        static Huffman h;
        static bool ready = false;
        if (!ready) {
            int lengths[288];
            for (int i = 0; i < 144; ++i) lengths[i] = 8;
            for (int i = 144; i < 256; ++i) lengths[i] = 9;
            for (int i = 256; i < 280; ++i) lengths[i] = 7;
            for (int i = 280; i < 288; ++i) lengths[i] = 8;
            buildHuffman(h, lengths, 288);
            ready = true;
        }
        return h;
    }

    const Huffman &fixedDistances()
    {
        static Huffman h;
        static bool ready = false;
        if (!ready) {
            int lengths[30];
            for (int i = 0; i < 30; ++i) lengths[i] = 5;
            buildHuffman(h, lengths, 30);
            ready = true;
        }
        return h;
    }

    int skipZeroTerminated(const QByteArray &data, int pos)
    {
        while (pos < data.size() && data.at(pos) != 0) ++pos;
        return pos + 1;
    }
}

QByteArray Inflate::inflateRaw(const QByteArray &data, int offset)
{
    State s(data, offset);
    Output out;
    bool final;
    do {
        final = s.readBits(1) == 1;
        int type = s.readBits(2);
        switch (type) {
        case 0: stored(s, out); break;
        case 1: compressed(s, out, fixedLengths(), fixedDistances()); break;
        case 2: dynamic(s, out); break;
        default: throw TlException(QLatin1String("invalid deflate block type 3"));
        }
    } while (!final);
    return out.buf;
}

QByteArray Inflate::gunzip(const QByteArray &data)
{
    if (data.size() < 18) throw TlException(QLatin1String("gzip stream too short"));
    const uchar *d = reinterpret_cast<const uchar *>(data.constData());
    if (d[0] != 0x1f || d[1] != 0x8b) throw TlException(QLatin1String("not a gzip stream"));
    if (d[2] != 8) throw TlException(QString::fromLatin1("unsupported gzip compression method %1").arg(d[2]));

    int flags = d[3];
    int pos = 10;
    if (flags & 0x04) {                       // FEXTRA
        int extra = d[pos] | (d[pos + 1] << 8);
        pos += 2 + extra;
    }
    if (flags & 0x08) pos = skipZeroTerminated(data, pos);   // FNAME
    if (flags & 0x10) pos = skipZeroTerminated(data, pos);   // FCOMMENT
    if (flags & 0x02) pos += 2;                              // FHCRC
    if (pos >= data.size()) throw TlException(QLatin1String("gzip header runs past the end of the data"));

    const int n = data.size();
    quint32 expectedSize = quint32(d[n - 4]) | (quint32(d[n - 3]) << 8) | (quint32(d[n - 2]) << 16) | (quint32(d[n - 1]) << 24);
    QByteArray result = inflateRaw(data, pos);
    if (quint32(result.size()) != expectedSize)
        throw TlException(QString::fromLatin1("gzip size mismatch: got %1, trailer says %2").arg(result.size()).arg(expectedSize));
    return result;
}
