// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "imagepicker.h"
#include "thumbnailer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QtAlgorithms>

namespace
{
    // Only what the app can actually send as a photo and render afterwards. GIF is left out on
    // purpose (animated ones are not supported, see MessageDelegate) - "File" still sends those.
    const char *const KPatterns[] = { "*.jpg", "*.jpeg", "*.png", "*.bmp" };

    // The places a Symbian phone keeps pictures, per drive, plus Symbigram's own download folder
    // so a received photo can be forwarded on. Visited in this order, and the whole list is
    // sorted by date at the end anyway, so a missing folder costs nothing.
    const char *const KFolders[] = {
        "Data/Images", "Images", "DCIM", "Data/Pictures", "Pictures", "Symbigram"
    };

    const int KMaxDepth = 4;      // Images/Camera/... is shallow; this is plenty
    const int KMaxDirs = 400;     // a sick or enormous library cannot make us walk forever
    const int KMaxFiles = 4000;   // beyond this the grid is unusable anyway
}

ImagePicker::ImagePicker(QObject *parent)
    : QAbstractListModel(parent)
    , m_timer(new QTimer(this))
    , m_thumbs(new Thumbnailer(this))
    , m_scanning(false)
    , m_dirsVisited(0)
{
    QHash<int, QByteArray> roles;
    roles[PathRole] = "path";
    roles[UrlRole] = "url";
    roles[ThumbRole] = "thumb";
    roles[ThumbFailedRole] = "thumbFailed";
    roles[NameRole] = "name";
    roles[FolderRole] = "folder";
    setRoleNames(roles);

    // Interval 0: one directory per turn of the event loop, so touch and paint still get served.
    m_timer->setInterval(0);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(scanSlice()));

    // The worker lives for the life of the model; a queued connection back onto this thread is
    // what the default Qt::AutoConnection gives us across a thread boundary.
    connect(m_thumbs, SIGNAL(ready(QString, QString)),
            this, SLOT(thumbReady(QString, QString)));
    m_thumbs->start(QThread::LowPriority);
}

int ImagePicker::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ImagePicker::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return QVariant();
    const Entry &e = m_rows.at(index.row());
    switch (role) {
    case PathRole: return e.path;
    case UrlRole: return QUrl::fromLocalFile(e.path).toString();
    case ThumbRole: return e.thumb.isEmpty() ? QString()
                                             : QUrl::fromLocalFile(e.thumb).toString();
    case ThumbFailedRole: return e.failed;
    case NameRole: return e.name;
    case FolderRole: return e.folder;
    default: return QVariant();
    }
}

QString ImagePicker::pathAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return QString();
    return QDir::toNativeSeparators(m_rows.at(row).path);
}

void ImagePicker::requestThumb(int row, int edge)
{
    if (row < 0 || row >= m_rows.size()) return;
    const Entry &e = m_rows.at(row);
    if (!e.thumb.isEmpty()) return;

    // Already cached from an earlier visit: take it now and skip the worker entirely.
    const QString cached = Thumbnailer::thumbPathFor(e.path);
    if (QFile::exists(cached)) {
        m_rows[row].thumb = cached;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx);
        return;
    }
    m_thumbs->request(e.path, edge);
}

void ImagePicker::thumbReady(const QString &imagePath, const QString &thumbPath)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).path != imagePath) continue;
        // An empty path means it could not be made - an unreadable picture, or nowhere to write
        // the cache (a card pulled mid-session). Recorded rather than ignored, so the cell shows
        // a placeholder instead of spinning for ever.
        if (thumbPath.isEmpty()) m_rows[i].failed = true;
        else m_rows[i].thumb = thumbPath;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx);
        return;
    }
}

bool ImagePicker::excluded(const QString &dir) const
{
    const QString clean = QDir::cleanPath(QDir(dir).absolutePath());
    for (int i = 0; i < m_excluded.size(); ++i)
        if (clean.compare(m_excluded.at(i), Qt::CaseInsensitive) == 0) return true;
    return false;
}

void ImagePicker::rescan()
{
    m_timer->stop();
    m_thumbs->clearQueue();
    m_found.clear();
    m_todo.clear();
    m_dirsVisited = 0;

    m_excluded.clear();
    const QStringList caches = Thumbnailer::allCacheDirs();
    for (int i = 0; i < caches.size(); ++i)
        m_excluded << QDir::cleanPath(QDir(caches.at(i)).absolutePath());

    // Every drive the phone currently has. QDir::drives() already reports only what is mounted,
    // so a removed card simply contributes nothing - no special case needed.
    const QFileInfoList drives = QDir::drives();
    for (int d = 0; d < drives.size(); ++d) {
        const QString root = drives.at(d).absolutePath();   // "C:/", "E:/", ...
        for (int f = 0; f < int(sizeof(KFolders) / sizeof(KFolders[0])); ++f) {
            const QString candidate = root + QLatin1String(KFolders[f]);
            if (!QFileInfo(candidate).isDir() || excluded(candidate)) continue;
            Dir dir;
            dir.path = candidate;
            dir.depth = 0;
            m_todo << dir;
        }
    }

    m_scanning = true;
    emit scanningChanged();
    emit foundChanged();
    if (m_todo.isEmpty()) finish();
    else m_timer->start();
}

void ImagePicker::scanSlice()
{
    if (m_todo.isEmpty() || m_dirsVisited >= KMaxDirs || m_found.size() >= KMaxFiles) {
        finish();
        return;
    }

    const Dir dir = m_todo.takeFirst();
    ++m_dirsVisited;
    QDir d(dir.path);

    QStringList patterns;
    for (int i = 0; i < int(sizeof(KPatterns) / sizeof(KPatterns[0])); ++i)
        patterns << QLatin1String(KPatterns[i]);

    const QFileInfoList files = d.entryInfoList(patterns, QDir::Files | QDir::Readable);
    for (int i = 0; i < files.size() && m_found.size() < KMaxFiles; ++i) {
        const QFileInfo &fi = files.at(i);
        if (fi.size() <= 0) continue;
        Entry e;
        e.failed = false;
        e.path = fi.absoluteFilePath();
        e.name = fi.fileName();
        e.folder = fi.absoluteDir().dirName();
        e.modified = fi.lastModified().toTime_t();
        m_found << e;
    }
    emit foundChanged();

    if (dir.depth < KMaxDepth) {
        const QFileInfoList subs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        for (int i = 0; i < subs.size(); ++i) {
            if (excluded(subs.at(i).absoluteFilePath())) continue;
            Dir sub;
            sub.path = subs.at(i).absoluteFilePath();
            sub.depth = dir.depth + 1;
            m_todo << sub;
        }
    }
}

void ImagePicker::finish()
{
    m_timer->stop();

    // Published in one go, newest first. Deliberately not appended as they are found: rows
    // appearing and then reordering under the user's finger is worse than a brief busy indicator,
    // and listing directories (no decoding) is quick even on a full card.
    qSort(m_found.begin(), m_found.end(), newerFirst);
    m_rows = m_found;
    m_found.clear();
    reset();

    m_scanning = false;
    emit scanningChanged();
    emit countChanged();
    emit foundChanged();
}
