// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Thumbnails for the image picker grid, made off the GUI thread and cached on disk.
//
// Why this exists: the grid first asked QML to load each full picture with sourceSize set. That
// still reads the whole file off flash and runs a (scaled) JPEG decode per cell, which on a phone
// with a few hundred camera photos is visibly slow. Two things fix it:
//
//   1. Camera JPEGs carry a small thumbnail inside their EXIF block - typically 160x120, which is
//      already about one grid cell. Reading that means touching ~10 KB instead of ~3 MB, with a
//      decode to match. Anything without one (screenshots, downloads, PNGs) falls back to a
//      scaled read, which libjpeg can do cheaply by DCT scaling.
//   2. The result is written to cacheDir() - a "thumbnails" folder inside whichever cache folder
//      the user chose - so opening the picker a second time costs one small file read per cell
//      and nothing else. That cache
//      deliberately survives "Clear cache": rebuilding it for a full card is exactly the slow
//      thing it exists to avoid, and it can be emptied by hand when space is short.
//
// All of it runs in this worker thread. The platform's own thumbnail server was the other option,
// but it belongs to the same media subsystem that made MGFetch hang (see imagepicker.h), and this
// way there is nothing to be at the mercy of.
#ifndef THUMBNAILER_H
#define THUMBNAILER_H

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

class Thumbnailer : public QThread
{
    Q_OBJECT

public:
    explicit Thumbnailer(QObject *parent = 0);
    ~Thumbnailer();

    /// Where thumbnails live: a "thumbnails" folder inside the cache folder, which AppController
    /// hands over with setCacheDir(). Settled once per run.
    static QString cacheDir();

    /// <drive>/Symbigram/thumbnails. Only for the fallback below and for the picker's exclusion
    /// list - the real path comes from the cache folder setting.
    static QString folderForDrive(const QString &drive);
    /// Used only until AppController sets the real one, and on the desktop harness. The first
    /// drive that can actually be written, mass memory first: a cache we cannot write is worse
    /// than one on the small drive.
    static QString defaultCacheDir();
    /// Points the cache at <cache folder>/thumbnails. Call before any Thumbnailer exists; an
    /// empty string restores the default. AppController calls this from its constructor, on the
    /// GUI thread, so the worker never races the static that remembers the path.
    static void setCacheDir(const QString &dir);
    /// The pre-1.0.103 location, cleaned up once at startup.
    static QString legacyCacheDir();
    /// Every folder thumbnails may occupy: all three drives under both the current name and the
    /// pre-1.0.129 one, the pre-1.0.103 location, and whatever is in use now. The image picker
    /// must exclude all of them from its scan - <drive>/Symbigram/thumbnails sits inside a folder
    /// the picker searches, so listing it would make the cache feed on itself. Being generous
    /// here is cheap: a folder that does not exist costs nothing to exclude, and now that the
    /// location is a setting, yesterday's choice is still on the drive.
    static QStringList allCacheDirs();
    static void discardLegacyCache();
    /// Renames a "thumbs" folder from 1.0.128 or earlier to "thumbnails", so a cache an older
    /// build already paid for is kept rather than rebuilt.
    static void adoptOldFolderName(const QString &dir);
    static qint64 cacheBytes();
    static qint64 clearCache();

    /// The cache file a picture would use, whether or not it exists yet. Keyed on path, size and
    /// modification time, so an edited or replaced picture gets a new thumbnail by itself.
    static QString thumbPathFor(const QString &imagePath);

    /// Queues a picture. Returns immediately; `ready` arrives later on the GUI thread. Call it
    /// only for cells in view - the queue is LIFO precisely so scrolling stays responsive.
    void request(const QString &imagePath, int edge);

    /// Drops anything not yet started (the user scrolled away).
    void clearQueue();

    void stop();

signals:
    /// thumbPath is the file now on disk, or empty if the picture could not be read at all.
    void ready(const QString &imagePath, const QString &thumbPath);

protected:
    void run();

public:
    /// The embedded EXIF thumbnail, or a null array when there is none. Parses just enough of the
    /// APP1/TIFF structure to find IFD1's JPEGInterchangeFormat tag. Public so it can be checked
    /// against real photos off the phone - a parser like this fails silently otherwise, and the
    /// only symptom would be the grid quietly staying slow.
    /// `orientation`, when given, receives the EXIF Orientation tag (1 if absent). Qt 4.7 has no
    /// setAutoTransform, so nothing rotates the pixels for us and the caller must do it.
    static QByteArray exifThumbnail(const QString &path, int *orientation = 0);

    /// Applies an EXIF Orientation value to an image. Needed for the scaled-read path too: that
    /// also yields stored pixels, and every other client shows the photograph rotated.
    static QImage applyOrientation(const QImage &image, int orientation);

private:
    struct Job {
        QString path;
        int edge;
    };

    QList<Job> m_queue;
    QMutex m_mutex;
    QWaitCondition m_wake;
    bool m_stop;
};

#endif // THUMBNAILER_H
