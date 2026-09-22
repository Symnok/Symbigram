// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "voiceplayer.h"
#include "opusvoice.h"

#include <QDir>
#include <QFile>
#include <QTimer>

#ifndef Q_OS_SYMBIAN
#include <QSound>
#endif

namespace
{
    // The phone's audio output is happiest at 16 kHz; voice needs nothing more.
    const int PlayRate = 16000;

#ifndef Q_OS_SYMBIAN
    void put32(QByteArray &b, quint32 v) { b.append(char(v)).append(char(v >> 8)).append(char(v >> 16)).append(char(v >> 24)); }
    void put16(QByteArray &b, quint16 v) { b.append(char(v)).append(char(v >> 8)); }
#endif
}

VoicePlayer::VoicePlayer(QObject *parent)
    : QObject(parent), m_playing(false)
{
    m_endTimer = new QTimer(this);
    m_endTimer->setSingleShot(true);
    connect(m_endTimer, SIGNAL(timeout()), this, SLOT(stop()));
#ifdef Q_OS_SYMBIAN
    m_stream = 0;
#else
    m_sound = 0;
#endif
}

VoicePlayer::~VoicePlayer()
{
    cleanup();
}

#ifndef Q_OS_SYMBIAN
bool VoicePlayer::writeWav(const QString &path, const QByteArray &pcm, int sampleRate, int channels)
{
    QByteArray hdr;
    const int byteRate = sampleRate * channels * 2;
    hdr.append("RIFF", 4);
    put32(hdr, 36 + pcm.size());
    hdr.append("WAVE", 4);
    hdr.append("fmt ", 4);
    put32(hdr, 16);
    put16(hdr, 1);                     // PCM
    put16(hdr, quint16(channels));
    put32(hdr, quint32(sampleRate));
    put32(hdr, quint32(byteRate));
    put16(hdr, quint16(channels * 2));
    put16(hdr, 16);
    hdr.append("data", 4);
    put32(hdr, pcm.size());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(hdr);
    f.write(pcm);
    f.close();
    return true;
}
#endif

bool VoicePlayer::play(const QString &oggPath, const QString &outputDir)
{
    stop();
    QFile f(oggPath);
    if (!f.open(QIODevice::ReadOnly)) { emit failed(tr("could not open the voice file")); return false; }
    QByteArray ogg = f.readAll();
    f.close();

    QString err;
    OpusVoice::Pcm pcm = OpusVoice::decode(ogg, PlayRate, &err);
    if (pcm.isEmpty()) { emit failed(err.isEmpty() ? tr("could not decode the voice message") : err); return false; }

#ifdef Q_OS_SYMBIAN
    Q_UNUSED(outputDir);
    m_pcm = pcm.data;
    TRAPD(createErr, m_stream = CMdaAudioOutputStream::NewL(*this));
    if (createErr != KErrNone || !m_stream) { emit failed(tr("audio is unavailable (%1)").arg(createErr)); return false; }
    m_settings.Query();
    m_settings.iChannels = (pcm.channels == 2) ? TMdaAudioDataSettings::EChannelsStereo
                                               : TMdaAudioDataSettings::EChannelsMono;
    m_settings.iSampleRate = TMdaAudioDataSettings::ESampleRate16000Hz;   // matches PlayRate
    TRAPD(openErr, m_stream->Open(&m_settings));
    if (openErr != KErrNone) { cleanup(); emit failed(tr("could not open audio (%1)").arg(openErr)); return false; }
    // Streaming starts in MaoscOpenComplete.
#else
    QString dir = outputDir;
    if (dir.isEmpty()) dir = QDir::tempPath();
    QDir().mkpath(dir);
    m_wavPath = dir + QLatin1String("/_voice.wav");
    if (!writeWav(m_wavPath, pcm.data, pcm.sampleRate, pcm.channels)) { emit failed(tr("could not prepare playback")); return false; }
    m_sound = new QSound(m_wavPath, this);
    m_sound->play();
    m_endTimer->start(pcm.durationMs() + 400);
#endif
    m_playing = true;
    emit started();
    return true;
}

void VoicePlayer::stop()
{
    bool was = m_playing;
    cleanup();
    if (was) emit stopped();
}

void VoicePlayer::cleanup()
{
    m_playing = false;
    if (m_endTimer) m_endTimer->stop();
#ifdef Q_OS_SYMBIAN
    if (m_stream) { m_stream->Stop(); delete m_stream; m_stream = 0; }
    m_pcm.clear();
#else
    if (m_sound) { m_sound->stop(); m_sound->deleteLater(); m_sound = 0; }
    if (!m_wavPath.isEmpty()) { QFile::remove(m_wavPath); m_wavPath.clear(); }
#endif
}

#ifdef Q_OS_SYMBIAN
void VoicePlayer::MaoscOpenComplete(TInt aError)
{
    if (aError != KErrNone || !m_stream) { cleanup(); emit failed(tr("could not start playback (%1)").arg(aError)); return; }
    TRAP_IGNORE(m_stream->SetVolume(m_stream->MaxVolume()));
    m_pcmPtr.Set(reinterpret_cast<const TUint8 *>(m_pcm.constData()), m_pcm.size());
    TRAPD(writeErr, m_stream->WriteL(m_pcmPtr));
    if (writeErr != KErrNone) { cleanup(); emit failed(tr("could not play (%1)").arg(writeErr)); }
}

void VoicePlayer::MaoscBufferCopied(TInt aError, const TDesC8 &)
{
    // The whole buffer was written in one WriteL, so there is nothing more to send. A real
    // error (not the KErrAbort that Stop() causes) ends playback.
    if (aError != KErrNone && aError != KErrAbort) { cleanup(); emit stopped(); }
}

void VoicePlayer::MaoscPlayComplete(TInt)
{
    // KErrUnderflow here is the normal end of a finite buffer; either way we are done.
    cleanup();
    emit stopped();
}
#endif
