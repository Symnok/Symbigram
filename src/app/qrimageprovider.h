// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Renders the login QR code for QML: Image { source: "image://qr/" + token } draws
// tg://login?token=<token> at a whole-pixel module scale (a fractional scale blurs the
// module edges and a camera reads the result far less reliably).
#ifndef QRIMAGEPROVIDER_H
#define QRIMAGEPROVIDER_H

#include <QDeclarativeImageProvider>

class QrImageProvider : public QDeclarativeImageProvider
{
public:
    QrImageProvider() : QDeclarativeImageProvider(QDeclarativeImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize);
};

#endif // QRIMAGEPROVIDER_H
