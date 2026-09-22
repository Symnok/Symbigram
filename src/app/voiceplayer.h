// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Plays a decoded voice message. The Opus/Ogg is turned into PCM by OpusVoice::decode; on
// the phone the raw 16-bit PCM is streamed straight to the audio device (CMdaAudioOutput-
// Stream) with the format set explicitly - no container, no file, no codec plugin to match
// (a file player returned KErrNotSupported for a plain WAV on some devices). On the desktop
// it writes a WAV and plays it with QSound. One message plays at a time.
#ifndef VOICEPLAYER_H
#define VOICEPLAYER_H

#include <QByteArray>
#include <QObject>
#include <QString>

#ifdef Q_OS_SYMBIAN
#include <mdaaudiooutputstream.h>
#include <mda/common/audio.h>
#endif

class QSound;
class QTimer;

class VoicePlayer : public QObject
#ifdef Q_OS_SYMBIAN
    , public MMdaAudioOutputStreamCallback
#endif
{
    Q_OBJECT
public:
    explicit VoicePlayer(QObject *parent = 0);
    ~VoicePlayer();

    /// Decodes the Opus/Ogg file at `oggPath` and starts playing it. `outputDir` is a public
    /// folder for the desktop WAV (ignored on the phone, which streams from memory).
    bool play(const QString &oggPath, const QString &outputDir = QString());
    bool playing() const { return m_playing; }

public slots:
    void stop();

signals:
    void started();
    void stopped();      // finished or was stopped
    void failed(const QString &error);

#ifdef Q_OS_SYMBIAN
public:
    void MaoscOpenComplete(TInt aError);
    void MaoscBufferCopied(TInt aError, const TDesC8 &aBuffer);
    void MaoscPlayComplete(TInt aError);
#endif

private:
    void cleanup();
#ifndef Q_OS_SYMBIAN
    static bool writeWav(const QString &path, const QByteArray &pcm, int sampleRate, int channels);
#endif

    bool m_playing;
    QTimer *m_endTimer;   // desktop: QSound has no finished signal, so time out at the end
#ifdef Q_OS_SYMBIAN
    CMdaAudioOutputStream *m_stream;
    TMdaAudioDataSettings m_settings;
    QByteArray m_pcm;     // kept alive while it is streamed
    TPtrC8 m_pcmPtr;
#else
    QString m_wavPath;
    QSound *m_sound;
#endif
};

#endif // VOICEPLAYER_H
