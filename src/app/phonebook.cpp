// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "phonebook.h"
#include "chatsmodel.h"      // initials()/colorFor-style helpers for the avatar circle

#include <QStringList>

#ifdef Q_OS_SYMBIAN
#include <cntdb.h>
#include <cntitem.h>
#include <cntfldst.h>
#include <cntdef.h>
#include <e32base.h>

namespace
{
    QString fromDes(const TDesC &d)
    {
        return QString::fromUtf16(reinterpret_cast<const ushort *>(d.Ptr()), d.Length());
    }
}
#endif

namespace
{
    // A stable colour per contact, so the circles are not all the same.
    QString colourFor(const QString &name)
    {
        static const char *const palette[] = {
            "#5b8fd0", "#5ab943", "#d08a5b", "#a05bd0", "#d05b7a", "#3fa8a0", "#c0a03f"
        };
        uint h = 0;
        for (int i = 0; i < name.size(); ++i) h = h * 31 + name.at(i).unicode();
        return QLatin1String(palette[h % 7]);
    }
}

PhoneBook::PhoneBook(QObject *parent)
    : QAbstractListModel(parent), m_loaded(false), m_loading(false)
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[NumberRole] = "number";
    roles[InitialsRole] = "initials";
    roles[ColorRole] = "color";
    setRoleNames(roles);
}

int PhoneBook::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

QVariant PhoneBook::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_view.size()) return QVariant();
    const Entry &e = m_view.at(index.row());
    switch (role) {
    case NameRole: return e.name;
    case NumberRole: return e.number;
    case InitialsRole: return ChatsModel::initials(e.name);
    case ColorRole: return colourFor(e.name);
    }
    return QVariant();
}

void PhoneBook::load()
{
    if (m_loaded || m_loading) return;
    reload();
}

void PhoneBook::reload()
{
    m_loading = true;
    m_error.clear();
    emit changed();

    m_all.clear();
    readAll();

    // Sorted by name, with the nameless (number-only) entries last.
    for (int i = 0; i < m_all.size(); ++i)
        for (int j = i + 1; j < m_all.size(); ++j)
            if (QString::localeAwareCompare(m_all.at(j).name, m_all.at(i).name) < 0)
                m_all.swap(i, j);

    m_loaded = true;
    m_loading = false;
    applyFilter();
}

void PhoneBook::setFilter(const QString &text)
{
    m_filter = text.trimmed();
    applyFilter();
}

void PhoneBook::applyFilter()
{
    beginResetModel();
    m_view.clear();
    const QString needle = m_filter.toLower();
    for (int i = 0; i < m_all.size(); ++i) {
        const Entry &e = m_all.at(i);
        if (needle.isEmpty() || e.name.toLower().contains(needle) || e.number.contains(needle))
            m_view.append(e);
    }
    endResetModel();
    emit changed();
}

#ifdef Q_OS_SYMBIAN

void PhoneBook::readAll()
{
    CContactDatabase *db = 0;
    TRAPD(openErr, db = CContactDatabase::OpenL());
    if (openErr != KErrNone || !db) {
        m_error = tr("Could not open the phonebook (%1).").arg(openErr);
        return;
    }

    TRAPD(readErr, readContactsL(db));
    delete db;

    if (readErr != KErrNone && m_all.isEmpty())
        m_error = tr("Could not read the phonebook (%1).").arg(readErr);
    else if (m_all.isEmpty())
        m_error = tr("No contacts with a phone number.");
}

void PhoneBook::readContactsL(void *dbPtr)
{
    CContactDatabase *db = static_cast<CContactDatabase *>(dbPtr);

    // Every contact. SortedItemsL is the cheap path; when the database has no sort order it comes
    // back empty, so fall back to "changed since the epoch", which means everything.
    const CContactIdArray *sorted = db->SortedItemsL();
    CContactIdArray *owned = 0;
    if (!sorted || sorted->Count() == 0) {
        owned = db->ContactsChangedSinceL(TTime(0));
        CleanupStack::PushL(owned);
    }
    const CContactIdArray *ids = owned ? owned : sorted;
    const TInt n = ids ? ids->Count() : 0;

    for (TInt i = 0; i < n; ++i) {
        CContactItem *item = 0;
        TRAPD(oneErr, item = db->ReadContactL((*ids)[i]));
        if (oneErr != KErrNone || !item) continue;
        CleanupStack::PushL(item);

        QString given;
        QString family;
        QString company;
        QStringList numbers;
        const CContactItemFieldSet &fields = item->CardFields();
        for (TInt f = 0; f < fields.Count(); ++f) {
            const CContactItemField &field = fields[f];
            if (field.StorageType() != KStorageTypeText) continue;
            const TUid type = field.ContentType().FieldType(0);
            const QString value = fromDes(field.TextStorage()->Text()).trimmed();
            if (value.isEmpty()) continue;
            if (type == KUidContactFieldGivenName) given = value;
            else if (type == KUidContactFieldFamilyName) family = value;
            else if (type == KUidContactFieldCompanyName) company = value;
            else if (type == KUidContactFieldPhoneNumber && !numbers.contains(value))
                numbers.append(value);
        }

        QString name = (given + QLatin1Char(' ') + family).simplified();
        if (name.isEmpty()) name = company;
        for (int k = 0; k < numbers.size(); ++k) {
            Entry e;
            e.number = numbers.at(k);
            e.name = name.isEmpty() ? e.number : name;
            m_all.append(e);
        }
        CleanupStack::PopAndDestroy(item);
    }
    if (owned) CleanupStack::PopAndDestroy(owned);
}

#else

void PhoneBook::readAll()
{
    // The desktop build has no phonebook; the page shows the explanation below.
    m_error = tr("The phone's address book is only available on the device.");
}

#endif
