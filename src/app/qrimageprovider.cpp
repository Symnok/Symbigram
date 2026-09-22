// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "qrimageprovider.h"
#include "qrcode.h"

#include <QImage>
#include <QPainter>

QImage QrImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QVector<bool> modules;
    int n = 0;
    if (id.isEmpty() || !QrCode::encode(QLatin1String("tg://login?token=") + id, modules, n)) {
        QImage empty(1, 1, QImage::Format_RGB32);
        empty.fill(0xffffffff);
        if (size) *size = empty.size();
        return empty;
    }
    const int quiet = 4;
    int target = requestedSize.width() > 0 ? requestedSize.width() : 300;
    int scale = qMax(1, target / (n + 2 * quiet));
    int pixels = (n + 2 * quiet) * scale;
    QImage img(pixels, pixels, QImage::Format_RGB32);
    img.fill(0xffffffff);
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            if (modules.at(r * n + c)) p.drawRect((c + quiet) * scale, (r + quiet) * scale, scale, scale);
    p.end();
    if (size) *size = img.size();
    return img;
}
