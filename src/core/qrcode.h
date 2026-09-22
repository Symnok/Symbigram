// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A QR Code encoder - byte mode, error correction level M, versions 1 to 15 - because QR
// login needs to *display* a code for another Telegram to scan, and nothing on the phone
// makes one. Level M is enough tolerance for a phone camera pointed at a screen without
// making the modules too small on a 360x640 display; a Telegram login URL fits in
// version 6. Ported from LumigramPlus, which verified it against a reference encoder.
#ifndef QRCODE_H
#define QRCODE_H

#include <QByteArray>
#include <QString>
#include <QVector>

class QrCode
{
public:
    /// Encodes text; empty on failure (text too long). The matrix is size x size modules,
    /// row-major, true = dark. forcedMask is for tests (-1 chooses the best mask).
    static bool encode(const QString &text, QVector<bool> &modules, int &size, int forcedMask = -1);
};

#endif // QRCODE_H
