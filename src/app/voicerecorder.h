// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Records a voice message. On the phone the microphone is captured as raw 16-bit PCM at
// 16 kHz mono (CMdaAudioInputStream), buffered, and encoded to Ogg/Opus with a waveform
// only when recording stops - so the Opus encoder (heavier than the decoder) never has to
// keep up in real time. Recording needs the UserEnvironment capability. Desktop builds
// cannot record (no microphone API here).
#ifndef VOICERECORDER_H
#define VOICERECORDER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTime>

#ifdef Q_OS_SYMBIAN
#include <mdaaudioinputstream.h>
#include <mda/common/audio.h>
#endif

class VoiceRecorder : public QObject
#ifdef Q_OS_SYMBIAN
    , public MMdaAudioInputStreamCallback
#endif
{
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject *parent = 0);
    ~VoiceRecorder();

    /// Starts capturing. `outputDir` is a public folder the encoded Ogg is written to.
    bool start(const QString &outputDir);
    /// Stops capturing, encodes, and emits recorded() (or failed()).
    void stop();
    /// Stops and throws the recording away.
    void cancel();
    bool recording() const { return m_recording; }
    int elapsedMs() const { return m_recording ? m_startTime.elapsed() : 0; }

signals:
    void started();
    void recorded(const QString &oggPath, int durationSec, const QByteArray &waveform);
    void failed(const QString &error);

#ifdef Q_OS_SYMBIAN
public:
    void MaiscOpenComplete(TInt aError);
    void MaiscBufferCopied(TInt aError, const TDesC8 &aBuffer);
    void MaiscRecordComplete(TInt aError);
#endif

private:
    void teardown();
    void encodeAndEmit();

    bool m_recording;
    bool m_wantEncode;      // stop() was asked while still opening
    QByteArray m_pcm;
    QString m_outputDir;
    QTime m_startTime;
#ifdef Q_OS_SYMBIAN
    CMdaAudioInputStream *m_stream;
    TMdaAudioDataSettings m_settings;
    TBuf8<4096> m_readBuf;
#endif
};

#endif // VOICERECORDER_H
