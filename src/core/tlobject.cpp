// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "tlobject.h"
#include "tlconstructors.h"

#include <QList>
#include <QStringList>

// -- TlObject ---------------------------------------------------------------------------------------

TlObject::TlObject() : d(new Data) {}
TlObject::TlObject(const TlObject &other) : d(other.d) {}
TlObject &TlObject::operator=(const TlObject &other) { d = other.d; return *this; }
TlObject::~TlObject() {}

bool TlObject::isNull() const { return d->entry == 0; }
quint32 TlObject::ctor() const { return d->ctor; }

QVariant TlObject::value(const char *name) const
{
    if (!d->entry) return QVariant();
    const QVector<QByteArray> &names = d->entry->names;
    for (int i = 0; i < names.size(); ++i)
        if (names.at(i) == name) return d->values.at(i);
    return QVariant();
}

bool TlObject::has(const char *name) const { return value(name).isValid(); }

int TlObject::intOr(const char *name, int fallback) const
{
    QVariant v = value(name);
    return v.isValid() ? v.toInt() : fallback;
}

qint64 TlObject::longOr(const char *name, qint64 fallback) const
{
    QVariant v = value(name);
    return v.isValid() ? v.toLongLong() : fallback;
}

double TlObject::doubleOr(const char *name, double fallback) const
{
    QVariant v = value(name);
    return v.isValid() ? v.toDouble() : fallback;
}

QString TlObject::str(const char *name) const { return value(name).toString(); }
QByteArray TlObject::bytes(const char *name) const { return value(name).toByteArray(); }
TlObject TlObject::obj(const char *name) const { return value(name).value<TlObject>(); }
QVariantList TlObject::vec(const char *name) const { return value(name).toList(); }

bool TlObject::flag(const char *flagsField, int bit) const
{
    QVariant v = value(flagsField);
    return v.isValid() && (v.toInt() & (1 << bit)) != 0;
}

QString TlObject::toString() const
{
    if (!d->entry) return QLatin1String("(null)");
    QStringList parts;
    for (int i = 0; i < d->entry->names.size(); ++i) {
        const QVariant &v = d->values.at(i);
        QString s;
        if (!v.isValid()) continue;
        if (v.canConvert<TlObject>()) s = v.value<TlObject>().toString();
        else if (v.type() == QVariant::List) s = QString::fromLatin1("[%1 items]").arg(v.toList().size());
        else if (v.type() == QVariant::ByteArray) s = QString::fromLatin1("<%1 bytes>").arg(v.toByteArray().size());
        else s = v.toString();
        parts.append(QString::fromLatin1(d->entry->names.at(i)) + QLatin1Char('=') + s);
    }
    return QString::fromLatin1("0x%1{").arg(d->ctor, 8, 16, QLatin1Char('0')) + parts.join(QLatin1String(", ")) + QLatin1Char('}');
}

// -- TlSchema ------------------------------------------------------------------------------------------

const QHash<quint32, TlObject::Entry *> &TlSchema::table()
{
    static QHash<quint32, TlObject::Entry *> *t = 0;
    if (t) return *t;
    t = new QHash<quint32, TlObject::Entry *>;
    t->reserve(3000);
    const char *p = packedTable;
    const char *end = packedTable + packedLength;
    while (p < end) {
        const char *semi = p;
        while (semi < end && *semi != ';') ++semi;
        // "<8 hex>:<fields>"
        if (semi - p >= 9) {
            quint32 ctor = QByteArray(p, 8).toUInt(0, 16);
            TlObject::Entry *e = new TlObject::Entry;
            const char *body = p + 9;
            if (semi > body) {
                QList<QByteArray> fields = QByteArray(body, int(semi - body)).split(',');
                e->names.reserve(fields.size());
                e->specs.reserve(fields.size());
                for (int i = 0; i < fields.size(); ++i) {
                    int eq = fields.at(i).indexOf('=');
                    e->names.append(fields.at(i).left(eq));
                    e->specs.append(fields.at(i).mid(eq + 1));
                }
            }
            t->insert(ctor, e);
        }
        p = semi + 1;
    }
    return *t;
}

bool TlSchema::isKnown(quint32 ctor)
{
    return table().contains(ctor);
}

TlObject TlSchema::readObject(TlReader &r)
{
    quint32 ctor = r.readConstructor();
    return readBody(r, ctor);
}

TlObject TlSchema::readBody(TlReader &r, quint32 ctor)
{
    if (ctor == Tl::Vector)
        throw TlException(QLatin1String("a bare Vector appeared where an object was expected"));
    const TlObject::Entry *e = table().value(ctor, 0);
    if (!e)
        throw TlException(QString::fromLatin1("unknown constructor 0x%1 (schema is layer %2)").arg(ctor, 8, 16, QLatin1Char('0')).arg(Layer));

    TlObject o;
    o.d->ctor = ctor;
    o.d->entry = e;
    o.d->values.resize(e->specs.size());
    QVector<int> flags;

    for (int i = 0; i < e->specs.size(); ++i) {
        QByteArray spec = e->specs.at(i);
        int q = spec.indexOf('?');
        if (q >= 0) {
            // "N.B?rest"
            int dot = spec.indexOf('.');
            int word = spec.left(dot).toInt();
            int bit = spec.mid(dot + 1, q - dot - 1).toInt();
            if (word >= flags.size())
                throw TlException(QString::fromLatin1("condition references flags word %1 before it was read").arg(word));
            if ((flags.at(word) & (1 << bit)) == 0) continue;   // absent: stays invalid
            spec = spec.mid(q + 1);
        }
        o.d->values[i] = readValue(r, spec, flags);
    }
    return o;
}

QVariant TlSchema::readValue(TlReader &r, const QByteArray &spec, QVector<int> &flags)
{
    switch (spec.at(0)) {
    case '#': {
        int v = r.readInt();
        flags.append(v);
        return v;
    }
    case 'i': return r.readInt();
    case 'l': return r.readLong();
    case 'd': return r.readDouble();
    case 's': return r.readString();
    case 'b': return r.readBytes();
    case 'I': return r.readRaw(16);
    case 'J': return r.readRaw(32);
    case 'o': {
        // Bool is built into the language rather than declared in the schema; it shows
        // up as a boxed object in a few places (e.g. account.password fields).
        quint32 c = r.peekConstructor();
        if (c == Tl::BoolTrue || c == Tl::BoolFalse) { r.readConstructor(); return c == Tl::BoolTrue; }
        return QVariant::fromValue(readObject(r));
    }
    case 'v': {
        quint32 vec = r.readConstructor();
        if (vec != Tl::Vector)
            throw TlException(QString::fromLatin1("expected a vector, got 0x%1").arg(vec, 8, 16, QLatin1Char('0')));
        return readElements(r, spec.mid(1), flags);
    }
    case 'V':
        return readElements(r, spec.mid(1), flags);
    default:
        throw TlException(QString::fromLatin1("unknown field spec '%1'").arg(QLatin1String(spec)));
    }
}

QVariantList TlSchema::readElements(TlReader &r, const QByteArray &elementSpec, QVector<int> &flags)
{
    int count = r.readInt();
    if (count < 0 || count > 1000000)
        throw TlException(QString::fromLatin1("implausible vector count %1").arg(count));
    QVariantList list;
    for (int i = 0; i < count; ++i) list.append(readValue(r, elementSpec, flags));
    return list;
}
