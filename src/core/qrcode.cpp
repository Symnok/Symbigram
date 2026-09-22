// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "qrcode.h"

#include <QList>
#include <cstdlib>

namespace
{
    // Per version (index 1..15) at EC level M: { ec codewords per block, group1 blocks,
    // group1 data codewords, group2 blocks, group2 data codewords }
    const int EcTableM[16][5] = {
        { 0, 0, 0, 0, 0 },
        { 10, 1, 16, 0, 0 }, { 16, 1, 28, 0, 0 }, { 26, 1, 44, 0, 0 }, { 18, 2, 32, 0, 0 }, { 24, 2, 43, 0, 0 },
        { 16, 4, 27, 0, 0 }, { 18, 4, 31, 0, 0 }, { 22, 2, 38, 2, 39 }, { 22, 3, 36, 2, 37 }, { 26, 4, 43, 1, 44 },
        { 30, 1, 50, 4, 51 }, { 22, 6, 36, 2, 37 }, { 22, 8, 37, 1, 38 }, { 24, 4, 40, 5, 41 }, { 24, 5, 41, 5, 42 },
    };

    const int AlignmentPositions[16][5] = {
        { 0 }, { 0 }, { 2, 6, 18 }, { 2, 6, 22 }, { 2, 6, 26 }, { 2, 6, 30 }, { 2, 6, 34 },
        { 3, 6, 22, 38 }, { 3, 6, 24, 42 }, { 3, 6, 26, 46 }, { 3, 6, 28, 50 }, { 3, 6, 30, 54 },
        { 3, 6, 32, 58 }, { 3, 6, 34, 62 }, { 4, 6, 26, 46, 66 }, { 4, 6, 26, 48, 70 },
    };   // first entry: how many positions follow

    // -- Reed-Solomon over GF(256) with the QR primitive polynomial 0x11D --
    uchar gfExp[512], gfLog[256];
    bool gfReady = false;

    void gfInit()
    {
        if (gfReady) return;
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            gfExp[i] = uchar(x);
            gfLog[x] = uchar(i);
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i) gfExp[i] = gfExp[i - 255];
        gfReady = true;
    }

    uchar gfMul(uchar a, uchar b)
    {
        if (a == 0 || b == 0) return 0;
        return gfExp[gfLog[a] + gfLog[b]];
    }

    QByteArray rsGenerator(int degree)
    {
        QByteArray result(degree, '\0');
        result[degree - 1] = 1;
        uchar root = 1;
        for (int i = 0; i < degree; ++i) {
            for (int j = 0; j < degree; ++j) {
                result[j] = char(gfMul(uchar(result.at(j)), root));
                if (j + 1 < degree) result[j] = char(uchar(result.at(j)) ^ uchar(result.at(j + 1)));
            }
            root = gfMul(root, 2);
        }
        return result;
    }

    QByteArray rsEncode(const QByteArray &data, int ecCount)
    {
        gfInit();
        QByteArray generator = rsGenerator(ecCount);
        QByteArray remainder(ecCount, '\0');
        for (int d = 0; d < data.size(); ++d) {
            uchar factor = uchar(data.at(d)) ^ uchar(remainder.at(0));
            remainder.remove(0, 1);
            remainder.append('\0');
            for (int i = 0; i < ecCount; ++i)
                remainder[i] = char(uchar(remainder.at(i)) ^ gfMul(uchar(generator.at(i)), factor));
        }
        return remainder;
    }

    // -- bit buffer --
    struct BitBuffer
    {
        QByteArray bytes;
        int bitLength;
        BitBuffer() : bitLength(0) {}
        void append(int value, int bits)
        {
            for (int i = bits - 1; i >= 0; --i) {
                if (bitLength % 8 == 0) bytes.append('\0');
                if ((value >> i) & 1) bytes[bitLength / 8] = char(uchar(bytes.at(bitLength / 8)) | (1 << (7 - (bitLength % 8))));
                ++bitLength;
            }
        }
    };

    int dataCodewords(int version)
    {
        const int *t = EcTableM[version];
        return t[1] * t[2] + t[3] * t[4];
    }

    int chooseVersion(int byteCount)
    {
        for (int v = 1; v <= 15; ++v) {
            int capacity = dataCodewords(v) - (v < 10 ? 2 : 3);
            if (byteCount <= capacity) return v;
        }
        return 0;
    }

    QByteArray buildCodewords(const QByteArray &data, int version)
    {
        int total = dataCodewords(version);
        BitBuffer bits;
        bits.append(0x4, 4);                                  // byte mode
        bits.append(data.size(), version < 10 ? 8 : 16);
        for (int i = 0; i < data.size(); ++i) bits.append(uchar(data.at(i)), 8);
        int capacityBits = total * 8;
        int terminator = qMin(4, capacityBits - bits.bitLength);
        bits.append(0, terminator);
        while (bits.bitLength % 8 != 0) bits.append(0, 1);
        QByteArray padded = bits.bytes;
        int written = padded.size();
        padded.resize(total);
        for (int i = written; i < total; ++i) padded[i] = ((i - written) % 2 == 0) ? char(0xEC) : char(0x11);
        return padded;
    }

    QByteArray interleaveWithEcc(const QByteArray &data, int version)
    {
        const int *t = EcTableM[version];
        int ecPerBlock = t[0], g1 = t[1], g1Len = t[2], g2 = t[3], g2Len = t[4];
        QList<QByteArray> blocks, eccs;
        int offset = 0;
        for (int i = 0; i < g1 + g2; ++i) {
            int len = i < g1 ? g1Len : g2Len;
            QByteArray block = data.mid(offset, len);
            offset += len;
            blocks.append(block);
            eccs.append(rsEncode(block, ecPerBlock));
        }
        QByteArray output;
        int maxData = qMax(g1Len, g2Len);
        for (int i = 0; i < maxData; ++i)
            for (int b = 0; b < blocks.size(); ++b)
                if (i < blocks.at(b).size()) output.append(blocks.at(b).at(i));
        for (int i = 0; i < ecPerBlock; ++i)
            for (int b = 0; b < eccs.size(); ++b)
                output.append(eccs.at(b).at(i));
        return output;
    }

    struct Matrix
    {
        int size;
        QVector<bool> m, reserved;
        explicit Matrix(int s) : size(s), m(s * s, false), reserved(s * s, false) {}
        bool at(int r, int c) const { return m.at(r * size + c); }
        void set(int r, int c, bool v) { m[r * size + c] = v; }
        void setReserved(int r, int c, bool v) { m[r * size + c] = v; reserved[r * size + c] = true; }
        bool isReserved(int r, int c) const { return reserved.at(r * size + c); }
    };

    void drawFinder(Matrix &x, int row, int col)
    {
        for (int r = -1; r <= 7; ++r)
            for (int c = -1; c <= 7; ++c) {
                int rr = row + r, cc = col + c;
                if (rr < 0 || rr >= x.size || cc < 0 || cc >= x.size) continue;
                bool dark = (r >= 0 && r <= 6 && (c == 0 || c == 6)) || (c >= 0 && c <= 6 && (r == 0 || r == 6)) || (r >= 2 && r <= 4 && c >= 2 && c <= 4);
                x.setReserved(rr, cc, dark);
            }
    }

    void drawAlignment(Matrix &x, int row, int col)
    {
        for (int r = -2; r <= 2; ++r)
            for (int c = -2; c <= 2; ++c)
                x.setReserved(row + r, col + c, qMax(abs(r), abs(c)) != 1);
    }

    void drawFunctionPatterns(Matrix &x, int version)
    {
        int size = x.size;
        drawFinder(x, 0, 0);
        drawFinder(x, size - 7, 0);
        drawFinder(x, 0, size - 7);
        for (int i = 8; i < size - 8; ++i) {
            bool dark = i % 2 == 0;
            x.setReserved(6, i, dark);
            x.setReserved(i, 6, dark);
        }
        const int *pos = AlignmentPositions[version];
        for (int a = 1; a <= pos[0]; ++a)
            for (int b = 1; b <= pos[0]; ++b) {
                int r = pos[a], c = pos[b];
                if ((r == 6 && c == 6) || (r == 6 && c == size - 7) || (r == size - 7 && c == 6)) continue;
                drawAlignment(x, r, c);
            }
        x.setReserved(size - 8, 8, true);                     // the always-dark module
        for (int i = 0; i < 9; ++i) {
            x.reserved[8 * size + i] = true;
            x.reserved[i * size + 8] = true;
        }
        for (int i = 0; i < 8; ++i) {
            x.reserved[8 * size + (size - 1 - i)] = true;
            x.reserved[(size - 1 - i) * size + 8] = true;
        }
        if (version >= 7)
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < 3; ++j) {
                    x.reserved[i * size + (size - 11 + j)] = true;
                    x.reserved[(size - 11 + j) * size + i] = true;
                }
    }

    void drawData(Matrix &x, const QByteArray &message)
    {
        int size = x.size;
        int bitIndex = 0;
        bool upward = true;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5;                          // the vertical timing column is skipped
            for (int i = 0; i < size; ++i) {
                int row = upward ? size - 1 - i : i;
                for (int c = 0; c < 2; ++c) {
                    int col = right - c;
                    if (x.isReserved(row, col)) continue;
                    bool bit = false;
                    if (bitIndex < message.size() * 8)
                        bit = ((uchar(message.at(bitIndex / 8)) >> (7 - (bitIndex % 8))) & 1) != 0;
                    x.set(row, col, bit);
                    ++bitIndex;
                }
            }
            upward = !upward;
        }
    }

    bool maskAt(int mask, int row, int col)
    {
        switch (mask) {
        case 0: return (row + col) % 2 == 0;
        case 1: return row % 2 == 0;
        case 2: return col % 3 == 0;
        case 3: return (row + col) % 3 == 0;
        case 4: return ((row / 2) + (col / 3)) % 2 == 0;
        case 5: return (row * col) % 2 + (row * col) % 3 == 0;
        case 6: return ((row * col) % 2 + (row * col) % 3) % 2 == 0;
        case 7: return ((row + col) % 2 + (row * col) % 3) % 2 == 0;
        default: return false;
        }
    }

    void applyMask(Matrix &x, int mask)
    {
        for (int r = 0; r < x.size; ++r)
            for (int c = 0; c < x.size; ++c)
                if (!x.isReserved(r, c) && maskAt(mask, r, c)) x.set(r, c, !x.at(r, c));
    }

    void drawFormatInfo(Matrix &x, int mask)
    {
        int size = x.size;
        int data = (0x00 << 3) | mask;                          // level M is 00
        int rem = data;
        for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ (((rem >> 9) & 1) * 0x537);
        int bits = ((data << 10) | rem) ^ 0x5412;
        for (int i = 0; i < 15; ++i) {
            bool bit = ((bits >> (14 - i)) & 1) != 0;
            if (i < 6) x.set(8, i, bit);
            else if (i == 6) x.set(8, 7, bit);
            else if (i == 7) x.set(8, 8, bit);
            else if (i == 8) x.set(7, 8, bit);
            else x.set(14 - i, 8, bit);
            if (i < 7) x.set(size - 1 - i, 8, bit);
            else x.set(8, size - 15 + i, bit);
        }
    }

    void drawVersionInfo(Matrix &x, int version)
    {
        int size = x.size;
        int rem = version;
        for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ (((rem >> 11) & 1) * 0x1F25);
        int bits = (version << 12) | rem;
        for (int i = 0; i < 18; ++i) {
            bool bit = ((bits >> i) & 1) != 0;
            int r = i / 3, c = size - 11 + (i % 3);
            x.set(r, c, bit);
            x.set(c, r, bit);
        }
    }

    int runPenalty(const Matrix &x, int line, bool horizontal)
    {
        int penalty = 0, run = 1;
        bool previous = horizontal ? x.at(line, 0) : x.at(0, line);
        for (int i = 1; i < x.size; ++i) {
            bool current = horizontal ? x.at(line, i) : x.at(i, line);
            if (current == previous) ++run;
            else {
                if (run >= 5) penalty += 3 + (run - 5);
                run = 1;
                previous = current;
            }
        }
        if (run >= 5) penalty += 3 + (run - 5);
        return penalty;
    }

    const bool FinderRun[7] = { true, false, true, true, true, false, true };

    bool matchesFinderRun(const Matrix &x, int a, int b, bool horizontal)
    {
        for (int i = 0; i < 7; ++i) {
            int r = horizontal ? a : b + i;
            int c = horizontal ? b + i : a;
            if (r >= x.size || c >= x.size) return false;
            if (x.at(r, c) != FinderRun[i]) return false;
        }
        return true;
    }

    int penalty(const Matrix &x)
    {
        int size = x.size, p = 0;
        for (int r = 0; r < size; ++r) { p += runPenalty(x, r, true); p += runPenalty(x, r, false); }
        for (int r = 0; r < size - 1; ++r)
            for (int c = 0; c < size - 1; ++c)
                if (x.at(r, c) == x.at(r, c + 1) && x.at(r, c) == x.at(r + 1, c) && x.at(r, c) == x.at(r + 1, c + 1)) p += 3;
        for (int r = 0; r < size; ++r)
            for (int c = 0; c < size - 6; ++c) {
                if (matchesFinderRun(x, r, c, true)) p += 40;
                if (matchesFinderRun(x, c, r, false)) p += 40;
            }
        int dark = 0;
        for (int i = 0; i < size * size; ++i) if (x.m.at(i)) ++dark;
        int percent = dark * 100 / (size * size);
        p += abs(percent - 50) / 5 * 10;
        return p;
    }

    int chooseMask(Matrix &x)
    {
        int best = 0, bestScore = 0x7fffffff;
        for (int mask = 0; mask < 8; ++mask) {
            applyMask(x, mask);
            drawFormatInfo(x, mask);
            int score = penalty(x);
            if (score < bestScore) { bestScore = score; best = mask; }
            applyMask(x, mask);                                 // masking is its own inverse
        }
        return best;
    }
}

bool QrCode::encode(const QString &text, QVector<bool> &modules, int &size, int forcedMask)
{
    QByteArray data = text.toUtf8();
    int version = chooseVersion(data.size());
    if (version == 0) return false;
    QByteArray codewords = buildCodewords(data, version);
    QByteArray finalMessage = interleaveWithEcc(codewords, version);

    size = 17 + version * 4;
    Matrix x(size);
    drawFunctionPatterns(x, version);
    drawData(x, finalMessage);
    int mask = forcedMask >= 0 ? forcedMask : chooseMask(x);
    applyMask(x, mask);
    drawFormatInfo(x, mask);
    if (version >= 7) drawVersionInfo(x, version);
    modules = x.m;
    return true;
}
