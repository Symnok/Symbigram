// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The image list behind "Attach > Image": the phone's picture folders, newest first.
//
// This replaces the platform's own picker (MGFetch, the Photos grid). That looked like the right
// answer - thumbnails and albums for free - but it froze the whole application on several phones,
// including a Nokia E7 that has no memory card slot at all, and there is no defending against it:
// MGFetch::RunL is modal and synchronous on the GUI thread, so when Photos or the metadata
// harvester stalls there is no call to time out and no event loop left to cancel from.
//
// Measured across three phones: it hung on a Nokia E7 (no card slot), hung on an N8 with a card
// in, and opened to a black empty grid on the same N8 with the card out. Nothing about the media
// library it dislikes is visible to us.
//
// So the scan is ours and is deliberately incremental: one directory per event-loop turn, which
// cannot block the UI however large or sick the media library is. Listing directories is cheap -
// the expensive part is decoding thumbnails, and QML does that lazily for visible cells only.
#ifndef IMAGEPICKER_H
#define IMAGEPICKER_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>

class QTimer;
class Thumbnailer;

class ImagePicker : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    /// Grows while the scan runs, so the page can say "Looking... 214 images" instead of nothing.
    Q_PROPERTY(int found READ found NOTIFY foundChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,   ///< native path, for sendFile
        UrlRole,                       ///< file:/// URL of the full picture
        ThumbRole,                     ///< file:/// URL of the cached thumbnail, "" until ready
        NameRole,
        FolderRole
    };

    explicit ImagePicker(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    int count() const { return m_rows.size(); }
    bool scanning() const { return m_scanning; }
    int found() const { return m_found.size(); }

    /// Restarts the scan. Cheap enough to call every time the page opens, which also means a photo
    /// taken since the last visit shows up without any cache to invalidate.
    Q_INVOKABLE void rescan();
    Q_INVOKABLE QString pathAt(int row) const;

    /// Asks for a row's thumbnail. Called by a delegate as it is created, so only cells in or
    /// near view are ever decoded; returns at once and the row updates when the file is there.
    Q_INVOKABLE void requestThumb(int row, int edge);

signals:
    void countChanged();
    void scanningChanged();
    void foundChanged();

private slots:
    void scanSlice();
    void thumbReady(const QString &imagePath, const QString &thumbPath);

private:
    struct Entry {
        QString path;
        QString name;
        QString folder;
        QString thumb;    ///< empty until the thumbnailer has one
        uint modified;
    };

    /// A directory still to visit, with its depth so the walk cannot run away.
    struct Dir {
        QString path;
        int depth;
    };

    void finish();
    static bool newerFirst(const Entry &a, const Entry &b) { return a.modified > b.modified; }

    QList<Entry> m_rows;      ///< published, sorted newest first
    QList<Entry> m_found;     ///< accumulated during the scan, unsorted
    QList<Dir> m_todo;        ///< directories still to visit
    QTimer *m_timer;
    Thumbnailer *m_thumbs;
    bool m_scanning;
    int m_dirsVisited;
};

#endif // IMAGEPICKER_H
