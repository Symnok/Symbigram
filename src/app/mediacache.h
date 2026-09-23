// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Maps attachments and profile pictures to files on the phone, downloading them through the
// session on demand and remembering where they landed. The models ask for a key (an
// attachment size, or a peer's photo) and either get a path back at once (already cached)
// or a ready() when the download finishes. One place so a picture shown in the list and the
// same picture opened in a chat share the one download and the one file.
#ifndef MEDIACACHE_H
#define MEDIACACHE_H

#include "tgtypes.h"

#include <QHash>
#include <QObject>
#include <QString>

class TelegramSession;

class MediaCache : public QObject
{
    Q_OBJECT
public:
    MediaCache(TelegramSession *session, QObject *parent = 0);

    /// Where the cache lives; created under the app data folder.
    void setDirectory(const QString &dir);

    /// The cached file for an attachment size, or empty. sizeType empty = the whole file
    /// (documents); a photo size name otherwise.
    QString cachedFile(const TgMedia &media, const QString &sizeType) const;
    /// Starts (or joins) a download; ready(key, path) or failed(key, error) follows. The key
    /// identifies it and is what cachedFile()/keyFor() return.
    QString fetch(const TgMedia &media, const QString &sizeType);
    QString keyFor(const TgMedia &media, const QString &sizeType) const;

    /// A peer's small profile picture: cached path, or empty (and a fetch is started).
    QString peerPhoto(const TgPeer &peer, const TgPeerInfo &info);
    QString peerPhotoKey(qint64 photoId) const;

    /// Reconstructs the full JPEG of a stripped inline thumbnail (Telegram's truncated
    /// form), written to a file; empty when there is none. An instant blurred placeholder.
    QString strippedThumbFile(const TgMedia &media);

    /// True while a fetch for this key is in flight.
    bool isFetching(const QString &key) const { return m_jobs.values().contains(key); }
    int progressPercent(const QString &key) const { return m_progress.value(key, 0); }

    /// Total size, in bytes, of everything in the cache directory.
    qint64 cacheBytes() const;
    /// Deletes cached files (skipping any download still in flight); returns bytes freed.
    qint64 clearCache();

signals:
    void ready(const QString &key, const QString &path);
    void failed(const QString &key, const QString &error);
    void progress(const QString &key, int percent);

private slots:
    void onDownloadFinished(int jobId, const QString &path);
    void onDownloadFailed(int jobId, const QString &error);
    void onDownloadProgress(int jobId, qint64 received, qint64 total);

private:
    QString pathForKey(const QString &key, const QString &ext) const;
    static QString extensionFor(const TgMedia &media);
    static QByteArray expandStrippedThumb(const QByteArray &stripped);

    TelegramSession *m_session;
    QString m_dir;
    QHash<int, QString> m_jobs;          // download job id -> key
    QHash<QString, QString> m_jobPath;   // key -> target path being written
    QHash<QString, int> m_progress;      // key -> percent
};

#endif // MEDIACACHE_H
