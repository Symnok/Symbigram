// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "thumbnailer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTransform>

namespace
{
    // Big enough for one grid cell on the widest screen these phones have, with a little to spare.
    // EXIF thumbnails are usually 160x120, which lands right here.
    const int KMaxEdge = 160;
    const int KJpegQuality = 82;

    // A JPEG segment carries a 16-bit length, so an APP1/EXIF block is at most this big. We no
    // longer read a fixed window from the front of the file: APP1 can sit behind other segments
    // (an ICC profile in APP2 is itself up to 64 KB), so the markers are walked with seeks and
    // only the EXIF segment itself is read. Measured on real camera files, the embedded thumbnail
    // is 512x384 and 48-58 KB - nothing like the 160x120 the format's origins suggest.
    const int KMaxSegment = 65533;
    const int KMaxMarkers = 64;

    // Readers over a TIFF block, honouring its endianness. Free functions rather than a local
    // struct holding a reference: GCCE 4.4 is old enough not to take chances with.
    quint16 tiffU16(const QByteArray &d, int base, bool big, int off)
    {
        if (base + off + 2 > d.size() || base + off < 0) return 0;
        const quint8 a = quint8(d.at(base + off));
        const quint8 b = quint8(d.at(base + off + 1));
        return big ? quint16((a << 8) | b) : quint16((b << 8) | a);
    }

    quint32 tiffU32(const QByteArray &d, int base, bool big, int off)
    {
        if (base + off + 4 > d.size() || base + off < 0) return 0;
        const quint8 a = quint8(d.at(base + off));
        const quint8 b = quint8(d.at(base + off + 1));
        const quint8 c = quint8(d.at(base + off + 2));
        const quint8 e = quint8(d.at(base + off + 3));
        return big ? ((quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(e))
                   : ((quint32(e) << 24) | (quint32(c) << 16) | (quint32(b) << 8) | quint32(a));
    }
}

Thumbnailer::Thumbnailer(QObject *parent)
    : QThread(parent)
    , m_stop(false)
{
}

Thumbnailer::~Thumbnailer()
{
    stop();
    wait();
}

QString Thumbnailer::cacheDir()
{
#ifdef Q_OS_SYMBIAN
    return QLatin1String("C:/Data/Symbigram/thumbs");
#else
    return QDir::homePath() + QLatin1String("/.symbigram/thumbs");
#endif
}

QString Thumbnailer::thumbPathFor(const QString &imagePath)
{
    const QFileInfo fi(imagePath);
    // Size and mtime are in the key, so a replaced picture never shows a stale thumbnail and
    // there is no invalidation to get wrong.
    QByteArray key = imagePath.toUtf8();
    key += '\0';
    key += QByteArray::number(qint64(fi.size()));
    key += '\0';
    key += QByteArray::number(qint64(fi.lastModified().toTime_t()));
    const QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex();
    return cacheDir() + QLatin1Char('/') + QString::fromLatin1(hash) + QLatin1String(".jpg");
}

qint64 Thumbnailer::cacheBytes()
{
    qint64 total = 0;
    const QFileInfoList files = QDir(cacheDir()).entryInfoList(QDir::Files);
    for (int i = 0; i < files.size(); ++i) total += files.at(i).size();
    return total;
}

qint64 Thumbnailer::clearCache()
{
    qint64 freed = 0;
    const QFileInfoList files = QDir(cacheDir()).entryInfoList(QDir::Files);
    for (int i = 0; i < files.size(); ++i) {
        const qint64 size = files.at(i).size();
        if (QFile::remove(files.at(i).absoluteFilePath())) freed += size;
    }
    return freed;
}

void Thumbnailer::request(const QString &imagePath, int edge)
{
    QMutexLocker lock(&m_mutex);
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue.at(i).path == imagePath) return;   // already waiting
    Job job;
    job.path = imagePath;
    job.edge = edge > 0 ? qMin(edge, KMaxEdge) : KMaxEdge;
    // Pushed at the front: the most recently revealed cells are the ones being looked at, so a
    // fast scroll does not leave the user waiting behind a queue of cells already gone past.
    m_queue.prepend(job);
    m_wake.wakeAll();
}

void Thumbnailer::clearQueue()
{
    QMutexLocker lock(&m_mutex);
    m_queue.clear();
}

void Thumbnailer::stop()
{
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_queue.clear();
    m_wake.wakeAll();
}

QByteArray Thumbnailer::exifThumbnail(const QString &path, int *orientation)
{
    if (orientation) *orientation = 1;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();

    char soi[2];
    if (f.read(soi, 2) != 2) return QByteArray();
    if (quint8(soi[0]) != 0xFF || quint8(soi[1]) != 0xD8) return QByteArray();   // not a JPEG

    const QByteArray exifMagic("Exif\0\0", 6);

    // Walk the markers, reading only each 4-byte header and skipping the payloads, until APP1
    // with an EXIF payload turns up. Cheap however far in it sits.
    for (int seen = 0; seen < KMaxMarkers; ++seen) {
        char hdr[4];
        if (f.read(hdr, 2) != 2) break;
        if (quint8(hdr[0]) != 0xFF) break;
        const quint8 marker = quint8(hdr[1]);
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;   // no payload
        if (marker == 0xDA || marker == 0xD9) break;                          // pixels start
        if (f.read(hdr + 2, 2) != 2) break;
        const int len = (int(quint8(hdr[2])) << 8) | int(quint8(hdr[3]));
        if (len < 2 || len > KMaxSegment + 2) break;
        const int payloadLen = len - 2;

        if (marker != 0xE1) {
            if (!f.seek(f.pos() + payloadLen)) break;
            continue;
        }

        const QByteArray seg = f.read(payloadLen);
        if (seg.size() != payloadLen) break;
        if (seg.size() < 6 || seg.left(6) != exifMagic) continue;   // APP1, but not EXIF (XMP)

        // Offsets in EXIF are relative to the start of the TIFF header, which is what `seg`
        // holds from byte 6 on; base 0 keeps the arithmetic the same as the specification's.
        const QByteArray tiff = seg.mid(6);
        if (tiff.size() < 8) return QByteArray();

        const bool big = tiff.left(2) == QByteArray("MM", 2);
        if (!big && tiff.left(2) != QByteArray("II", 2)) return QByteArray();

        const quint32 ifd0 = tiffU32(tiff, 0, big, 4);
        if (ifd0 < 8 || ifd0 > quint32(tiff.size())) return QByteArray();
        const quint16 n0 = tiffU16(tiff, 0, big, int(ifd0));
        if (n0 == 0 || n0 > 512) return QByteArray();

        // Orientation lives in IFD0, which we are already walking past.
        if (orientation) {
            for (int i = 0; i < int(n0); ++i) {
                const int e = int(ifd0) + 2 + 12 * i;
                if (tiffU16(tiff, 0, big, e) != 0x0112) continue;
                const int v = int(tiffU16(tiff, 0, big, e + 8));
                if (v >= 1 && v <= 8) *orientation = v;
                break;
            }
        }

        // IFD1 - the thumbnail directory - is linked immediately after IFD0's entries.
        const quint32 ifd1 = tiffU32(tiff, 0, big, int(ifd0) + 2 + 12 * int(n0));
        if (ifd1 < 8 || ifd1 > quint32(tiff.size())) return QByteArray();
        const quint16 n1 = tiffU16(tiff, 0, big, int(ifd1));
        if (n1 == 0 || n1 > 512) return QByteArray();

        quint32 thumbOff = 0;
        quint32 thumbLen = 0;
        for (int i = 0; i < int(n1); ++i) {
            const int e = int(ifd1) + 2 + 12 * i;
            const quint16 tag = tiffU16(tiff, 0, big, e);
            if (tag == 0x0201) thumbOff = tiffU32(tiff, 0, big, e + 8);        // offset
            else if (tag == 0x0202) thumbLen = tiffU32(tiff, 0, big, e + 8);   // byte count
        }
        if (thumbOff == 0 || thumbLen == 0) return QByteArray();
        if (qint64(thumbOff) + qint64(thumbLen) > qint64(tiff.size())) return QByteArray();
        return tiff.mid(int(thumbOff), int(thumbLen));
    }
    return QByteArray();
}

QImage Thumbnailer::applyOrientation(const QImage &image, int orientation)
{
    if (orientation <= 1 || orientation > 8 || image.isNull()) return image;

    QTransform t;
    switch (orientation) {
    case 2: return image.mirrored(true, false);
    case 3: t.rotate(180); break;
    case 4: return image.mirrored(false, true);
    case 5: t.rotate(90); return image.transformed(t).mirrored(true, false);
    case 6: t.rotate(90); break;
    case 7: t.rotate(270); return image.transformed(t).mirrored(true, false);
    case 8: t.rotate(270); break;
    default: return image;
    }
    return image.transformed(t);
}

void Thumbnailer::run()
{
    QDir().mkpath(cacheDir());

    for (;;) {
        Job job;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stop && m_queue.isEmpty()) m_wake.wait(&m_mutex);
            if (m_stop) return;
            job = m_queue.takeFirst();
        }

        const QString out = thumbPathFor(job.path);
        if (QFile::exists(out)) {
            emit ready(job.path, out);
            continue;
        }

        QImage image;
        int orientation = 1;

        // The stored (un-rotated) size of the real picture, from the header alone - no decode.
        // Used to sanity-check the embedded thumbnail below.
        const QSize storedSize = QImageReader(job.path).size();

        // Fast path: the picture's own EXIF thumbnail. Tens of KB instead of several MB.
        const QByteArray embedded = exifThumbnail(job.path, &orientation);
        if (!embedded.isEmpty()) {
            // Scaled even here: the embedded thumbnail is itself 512x384 on real cameras, so
            // letting the reader decode it at 1/2 or 1/4 is worth the few lines.
            QBuffer buffer;
            buffer.setData(embedded);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer, "JPEG");
            const QSize full = reader.size();
            if (full.isValid() && (full.width() > job.edge || full.height() > job.edge)) {
                QSize target = full;
                target.scale(job.edge, job.edge, Qt::KeepAspectRatio);
                reader.setScaledSize(target);
            }
            image = reader.read();

            // An embedded thumbnail can be stale: editors crop or rotate the picture and leave
            // the old one in place. Seen in practice - a 4608x4608 crop still carrying its
            // original 4:3 thumbnail, which would show the grid one framing and send another.
            // Aspect is the cheap tell; when it disagrees, throw the thumbnail away.
            if (!image.isNull() && storedSize.isValid() && image.height() > 0 && storedSize.height() > 0) {
                const qreal a = qreal(image.width()) / qreal(image.height());
                const qreal b = qreal(storedSize.width()) / qreal(storedSize.height());
                if (qAbs(a - b) > 0.05 * b) image = QImage();
            }
        }

        // Otherwise a scaled read: setScaledSize lets the JPEG reader decode at 1/2, 1/4 or 1/8
        // scale rather than unpacking the full image and throwing most of it away.
        if (image.isNull()) {
            QImageReader reader(job.path);
            const QSize full = reader.size();
            if (full.isValid() && (full.width() > job.edge || full.height() > job.edge)) {
                QSize target = full;
                target.scale(job.edge, job.edge, Qt::KeepAspectRatio);
                reader.setScaledSize(target);
            }
            image = reader.read();
        }

        if (image.isNull()) {
            emit ready(job.path, QString());   // unreadable; the grid shows a placeholder
            continue;
        }

        // Both paths hand back stored pixels, so the rotation is applied here, once, to whichever
        // one produced the image. Without it a portrait photo shows sideways in the grid while
        // every other client displays it upright.
        image = applyOrientation(image, orientation);

        if (image.width() > job.edge || image.height() > job.edge)
            image = image.scaled(job.edge, job.edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        emit ready(job.path, image.save(out, "JPEG", KJpegQuality) ? out : QString());
    }
}
