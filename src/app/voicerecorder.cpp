// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "voicerecorder.h"
#include "opusvoice.h"

#include <QDir>
#include <QFile>

namespace
{
    const int RecordRate = 16000;      // 16 kHz mono, matching playback
    const int MaxSeconds = 300;        // a hard cap so a forgotten recording cannot grow forever
}

VoiceRecorder::VoiceRecorder(QObject *parent)
    : QObject(parent), m_recording(false), m_wantEncode(false)
{
#ifdef Q_OS_SYMBIAN
    m_stream = 0;
#endif
}

VoiceRecorder::~VoiceRecorder()
{
    teardown();
}

bool VoiceRecorder::start(const QString &outputDir)
{
    cancel();
    m_outputDir = outputDir;
    m_pcm.clear();
    m_wantEncode = false;
#ifdef Q_OS_SYMBIAN
    TRAPD(createErr, m_stream = CMdaAudioInputStream::NewL(*this));
    if (createErr != KErrNone || !m_stream) { emit failed(tr("the microphone is unavailable (%1)").arg(createErr)); return false; }
    m_settings.Query();
    m_settings.iChannels = TMdaAudioDataSettings::EChannelsMono;
    m_settings.iSampleRate = TMdaAudioDataSettings::ESampleRate16000Hz;
    TRAPD(openErr, m_stream->Open(&m_settings));
    if (openErr != KErrNone) { teardown(); emit failed(tr("could not open the microphone (%1)").arg(openErr)); return false; }
    m_recording = true;
    m_startTime.start();
    emit started();
    return true;
#else
    Q_UNUSED(outputDir);
    emit failed(tr("recording is only available on the phone"));
    return false;
#endif
}

void VoiceRecorder::stop()
{
    if (!m_recording) return;
#ifdef Q_OS_SYMBIAN
    if (m_stream) m_stream->Stop();
#endif
    m_recording = false;
    encodeAndEmit();
    teardown();
}

void VoiceRecorder::cancel()
{
    m_recording = false;
    m_pcm.clear();
    teardown();
}

void VoiceRecorder::teardown()
{
#ifdef Q_OS_SYMBIAN
    if (m_stream) { m_stream->Stop(); delete m_stream; m_stream = 0; }
#endif
}

void VoiceRecorder::encodeAndEmit()
{
    if (m_pcm.size() < RecordRate / 2 * 2) {     // under ~0.5 s of audio: too short to send
        emit failed(tr("the recording was too short"));
        m_pcm.clear();
        return;
    }
    QString err;
    OpusVoice::Encoded en = OpusVoice::encode(m_pcm, RecordRate, 1, &err);
    m_pcm.clear();
    if (en.isEmpty()) { emit failed(err.isEmpty() ? tr("could not encode the recording") : err); return; }

    QString dir = m_outputDir;
    if (dir.isEmpty()) dir = QDir::tempPath();
    QDir().mkpath(dir);
    QString path = dir + QLatin1String("/_rec.ogg");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit failed(tr("could not save the recording")); return; }
    f.write(en.ogg);
    f.close();
    emit recorded(path, (en.durationMs + 500) / 1000, en.waveform);
}

#ifdef Q_OS_SYMBIAN
void VoiceRecorder::MaiscOpenComplete(TInt aError)
{
    if (aError != KErrNone || !m_stream) { teardown(); m_recording = false; emit failed(tr("could not start recording (%1)").arg(aError)); return; }
    TRAP_IGNORE(m_stream->SetGain(m_stream->MaxGain()));
    m_readBuf.Zero();
    TRAP_IGNORE(m_stream->ReadL(m_readBuf));
}

void VoiceRecorder::MaiscBufferCopied(TInt aError, const TDesC8 &aBuffer)
{
    if (aError != KErrNone) return;              // KErrAbort arrives when we Stop(); just stop reading
    m_pcm.append(reinterpret_cast<const char *>(aBuffer.Ptr()), aBuffer.Length());
    if (!m_recording || m_pcm.size() >= RecordRate * 2 * MaxSeconds) return;
    m_readBuf.Zero();
    TRAP_IGNORE(m_stream->ReadL(m_readBuf));
}

void VoiceRecorder::MaiscRecordComplete(TInt)
{
    // Reading finished (we asked it to, or an error). The PCM already gathered is used by
    // stop(); nothing more to do here.
}
#endif
