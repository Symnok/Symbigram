// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "opusvoice.h"
#include "ogg.h"
#include "crypto.h"

#include <opus.h>
#include <QVector>
#include <string.h>

namespace
{
    quint16 rd16(const uchar *p) { return quint16(p[0]) | (quint16(p[1]) << 8); }
}

namespace OpusVoice
{

Pcm decode(const QByteArray &ogg, int outRate, QString *error)
{
    Pcm out;
    if (outRate != 8000 && outRate != 12000 && outRate != 16000 && outRate != 24000 && outRate != 48000) {
        if (error) *error = QString::fromLatin1("unsupported output rate");
        return out;
    }
    OggReader reader(ogg);
    QByteArray packet;
    qint64 granule = 0;

    // First packet: the identification header (OpusHead).
    if (!reader.nextPacket(packet, granule) || packet.size() < 19 || !packet.startsWith("OpusHead")) {
        if (error) *error = QString::fromLatin1("not an Opus voice stream");
        return out;
    }
    const uchar *h = reinterpret_cast<const uchar *>(packet.constData());
    int channels = h[9];
    int preSkip48 = rd16(h + 10);            // pre-skip is counted in 48 kHz samples
    if (channels < 1 || channels > 2) {
        if (error) *error = QString::fromLatin1("unsupported channel count");
        return out;
    }

    // Second packet: the comment header (OpusTags) - skipped.
    if (!reader.nextPacket(packet, granule) || !packet.startsWith("OpusTags")) {
        if (error) *error = QString::fromLatin1("missing Opus tags header");
        return out;
    }

    int err = 0;
    OpusDecoder *dec = opus_decoder_create(outRate, channels, &err);
    if (!dec || err != OPUS_OK) {
        if (error) *error = QString::fromLatin1("could not create the Opus decoder");
        if (dec) opus_decoder_destroy(dec);
        return out;
    }

    const int maxFrame = outRate / 1000 * 120;   // 120 ms, the largest an Opus packet decodes to
    QByteArray frameBuf;
    frameBuf.resize(maxFrame * channels * 2);
    opus_int16 *pcm = reinterpret_cast<opus_int16 *>(frameBuf.data());

    QByteArray all;
    while (reader.nextPacket(packet, granule)) {
        if (packet.isEmpty()) continue;
        int got = opus_decode(dec, reinterpret_cast<const unsigned char *>(packet.constData()),
                              packet.size(), pcm, maxFrame, 0);
        if (got < 0) continue;               // a bad packet: skip it rather than abort the message
        all.append(frameBuf.constData(), got * channels * 2);
    }
    opus_decoder_destroy(dec);

    // Drop the encoder delay (pre-skip), scaled from 48 kHz to the output rate.
    const int preSkip = int(qint64(preSkip48) * outRate / 48000);
    const int skipBytes = qMin(preSkip * channels * 2, all.size());
    all.remove(0, skipBytes);

    out.data = all;
    out.sampleRate = outRate;
    out.channels = channels;
    if (!reader.ok() && all.isEmpty() && error) *error = QString::fromLatin1("the voice file is damaged");
    return out;
}

QByteArray waveform(const QByteArray &pcm16, int channels, int count)
{
    const int frames = channels > 0 ? pcm16.size() / (2 * channels) : 0;
    if (frames <= 0 || count <= 0) return QByteArray();
    const qint16 *pcm = reinterpret_cast<const qint16 *>(pcm16.constData());
    QVector<int> peaks(count, 0);
    int globalMax = 1;
    for (int i = 0; i < count; ++i) {
        int start = int(qint64(i) * frames / count);
        int end = int(qint64(i + 1) * frames / count);
        if (end <= start) end = start + 1;
        int peak = 0;
        for (int f = start; f < end && f < frames; ++f) {
            int v = pcm[f * channels];
            if (v < 0) v = -v;
            if (v > peak) peak = v;
        }
        peaks[i] = peak;
        if (peak > globalMax) globalMax = peak;
    }
    // Scale to 0..31 and pack 5 bits each, least-significant bit first (Telegram's layout).
    QByteArray packed((count * 5 + 7) / 8, char(0));
    for (int i = 0; i < count; ++i) {
        int v = int(qint64(peaks[i]) * 31 / globalMax);
        if (v > 31) v = 31;
        const int bit = i * 5;
        const int byteIndex = bit >> 3;
        const int off = bit & 7;
        packed[byteIndex] = char(uchar(packed[byteIndex]) | ((v << off) & 0xff));
        if (off > 3 && byteIndex + 1 < packed.size())
            packed[byteIndex + 1] = char(uchar(packed[byteIndex + 1]) | (v >> (8 - off)));
    }
    return packed;
}

Encoded encode(const QByteArray &pcm16, int inRate, int channels, QString *error)
{
    Encoded out;
    if ((inRate != 8000 && inRate != 12000 && inRate != 16000 && inRate != 24000 && inRate != 48000) ||
        channels < 1 || channels > 2 || pcm16.isEmpty()) {
        if (error) *error = QString::fromLatin1("bad PCM for encoding");
        return out;
    }
    int err = 0;
    OpusEncoder *enc = opus_encoder_create(inRate, channels, OPUS_APPLICATION_VOIP, &err);
    if (!enc || err != OPUS_OK) {
        if (error) *error = QString::fromLatin1("could not create the Opus encoder");
        if (enc) opus_encoder_destroy(enc);
        return out;
    }
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(20000));
    int lookahead = 0;
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
    const int preSkip48 = int(qint64(lookahead) * 48000 / inRate);

    OggWriter w(qint32(Crypto::randomUInt64()));
    QByteArray head("OpusHead");
    head.append(char(1)).append(char(channels));
    head.append(char(preSkip48 & 0xff)).append(char((preSkip48 >> 8) & 0xff));
    head.append(char(inRate & 0xff)).append(char((inRate >> 8) & 0xff)).append(char((inRate >> 16) & 0xff)).append(char((inRate >> 24) & 0xff));
    head.append(char(0)).append(char(0)).append(char(0));       // output gain 0, mapping family 0
    w.addPacket(head, 0, true, false);
    QByteArray tags("OpusTags");
    const char *vendor = "Symbigram";
    const int vlen = 9;
    tags.append(char(vlen)).append(char(0)).append(char(0)).append(char(0)).append(vendor, vlen);
    tags.append(char(0)).append(char(0)).append(char(0)).append(char(0));   // user comment count 0
    w.addPacket(tags, 0, true, false);

    const int frame = inRate / 1000 * 20;                       // 20 ms
    const int totalFrames = pcm16.size() / (2 * channels);
    const qint16 *pcm = reinterpret_cast<const qint16 *>(pcm16.constData());
    QByteArray pktBuf;
    pktBuf.resize(4000);
    unsigned char *pkt = reinterpret_cast<unsigned char *>(pktBuf.data());
    QVector<qint16> padded;
    qint64 granule = 0;
    int pos = 0;
    while (pos < totalFrames) {
        const int avail = totalFrames - pos;
        const qint16 *in;
        if (avail >= frame) {
            in = pcm + qint64(pos) * channels;
        } else {                                                // pad the final short frame with silence
            padded.fill(0, frame * channels);
            memcpy(padded.data(), pcm + qint64(pos) * channels, avail * channels * 2);
            in = padded.constData();
        }
        int nb = opus_encode(enc, in, frame, pkt, 4000);
        pos += frame;
        granule += qint64(frame) * 48000 / inRate;              // granule counts 48 kHz samples
        if (nb > 0) w.addPacket(QByteArray(reinterpret_cast<char *>(pkt), nb), granule, false, pos >= totalFrames);
    }
    opus_encoder_destroy(enc);

    out.ogg = w.result();
    out.durationMs = int(qint64(totalFrames) * 1000 / inRate);
    out.waveform = waveform(pcm16, channels, 100);
    return out;
}

}
