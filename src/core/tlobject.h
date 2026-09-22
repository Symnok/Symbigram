// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// A parsed TL object - its constructor id plus its fields, addressed by name - and the
// schema-driven reader that produces it. Values are QVariants: int, qint64, double,
// QString, QByteArray, a nested TlObject, or a QVariantList for vectors. A field whose
// flag bit was clear is present with an invalid QVariant, so asking for it is not an error.
//
// The reader walks the generated field table (tlschema_data.cpp) instead of hand-written
// parsers per type: message#7600b9d3 alone reaches a dozen further types through its
// optional fields, and TL elements carry no length, so reading element two of a vector
// requires having consumed element one exactly.
#ifndef TLOBJECT_H
#define TLOBJECT_H

#include "tlreader.h"

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QSharedData>
#include <QSharedDataPointer>
#include <QString>
#include <QVariant>
#include <QVector>

class TlObject
{
public:
    TlObject();
    TlObject(const TlObject &other);
    TlObject &operator=(const TlObject &other);
    ~TlObject();

    bool isNull() const;
    quint32 ctor() const;
    /// True if the field exists in this constructor and was present on the wire.
    bool has(const char *name) const;
    /// The raw value; invalid when absent or unknown.
    QVariant value(const char *name) const;

    int intOr(const char *name, int fallback = 0) const;
    qint64 longOr(const char *name, qint64 fallback = 0) const;
    double doubleOr(const char *name, double fallback = 0) const;
    QString str(const char *name) const;
    QByteArray bytes(const char *name) const;
    TlObject obj(const char *name) const;
    QVariantList vec(const char *name) const;
    /// True if the given bit of the named flags word is set.
    bool flag(const char *flagsField, int bit) const;

    QString toString() const;

    struct Entry
    {
        QVector<QByteArray> names;
        QVector<QByteArray> specs;
    };

private:
    friend class TlSchema;
    struct Data : public QSharedData
    {
        Data() : ctor(0), entry(0) {}
        quint32 ctor;
        const Entry *entry;
        QVector<QVariant> values;
    };
    QSharedDataPointer<Data> d;
};

Q_DECLARE_METATYPE(TlObject)

class TlSchema
{
public:
    static const int Layer;
    static bool isKnown(quint32 ctor);
    /// Reads a constructor id and the object that follows it.
    static TlObject readObject(TlReader &r);
    static TlObject readBody(TlReader &r, quint32 ctor);
    /// Helper for QVariantList elements that are objects.
    static TlObject toObject(const QVariant &v) { return v.value<TlObject>(); }

private:
    static const char *const packedTable;
    static const int packedLength;
    static const QHash<quint32, TlObject::Entry *> &table();
    static QVariant readValue(TlReader &r, const QByteArray &spec, QVector<int> &flags);
    static QVariantList readElements(TlReader &r, const QByteArray &elementSpec, QVector<int> &flags);
};

#endif // TLOBJECT_H
