// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Turns a Telegram voice message (Opus packets in an Ogg container, RFC 7845) into signed
// 16-bit PCM the phone's audio output can play. Opus decoding is done by the vendored
// libopus (third_party/opus); this wraps the Ogg demux, the two Opus headers, the pre-skip
// and the output sample-rate choice.
#ifndef OPUSVOICE_H
#define OPUSVOICE_H

#include <QByteArray>
#include <QString>

namespace OpusVoice
{
    struct Pcm
    {
        Pcm() : sampleRate(0), channels(0) {}
        QByteArray data;      // interleaved signed 16-bit little-endian
        int sampleRate;
        int channels;
        bool isEmpty() const { return data.isEmpty(); }
        int frames() const { return channels > 0 ? data.size() / (2 * channels) : 0; }
        int durationMs() const { return sampleRate > 0 ? int(qint64(frames()) * 1000 / sampleRate) : 0; }
    };

    /// Decodes the whole file to PCM at `outRate` (8000/12000/16000/24000/48000). Returns an
    /// empty Pcm and sets `error` on failure.
    Pcm decode(const QByteArray &ogg, int outRate, QString *error = 0);

    struct Encoded
    {
        Encoded() : durationMs(0) {}
        QByteArray ogg;         // an Ogg/Opus voice stream (RFC 7845)
        int durationMs;
        QByteArray waveform;    // Telegram's 5-bit-per-sample amplitude envelope, 0..31
        bool isEmpty() const { return ogg.isEmpty(); }
    };

    /// Encodes signed 16-bit PCM (mono/stereo at inRate) as an Ogg/Opus voice message and
    /// computes the amplitude waveform. Returns an empty Encoded on failure.
    Encoded encode(const QByteArray &pcm16, int inRate, int channels, QString *error = 0);

    /// The Telegram waveform for some PCM: `count` peak samples scaled to 0..31 and packed
    /// 5 bits each, LSB first.
    QByteArray waveform(const QByteArray &pcm16, int channels, int count = 100);
}

#endif // OPUSVOICE_H
