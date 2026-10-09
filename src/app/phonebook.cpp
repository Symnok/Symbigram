// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
#include "phonebook.h"
#include "chatsmodel.h"      // initials()/colorFor-style helpers for the avatar circle

#include <QStringList>
#include <QVector>

#ifdef Q_OS_SYMBIAN
#include <cntdb.h>
#include <cntitem.h>
#include <cntfield.h>
#include <cntfldst.h>
#include <cntdef.h>
#include <e32base.h>

namespace
{
    QString fromDes(const TDesC &d)
    {
        return QString::fromUtf16(reinterpret_cast<const ushort *>(d.Ptr()), d.Length());
    }

    TPtrC toPtr(const QString &s)
    {
        return TPtrC(reinterpret_cast<const TUint16 *>(s.utf16()), s.length());
    }

    /// One text field, labelled the way the native Contacts application labels them.
    void addTextFieldL(CContactItem &item, TUid contentType, TUid vcardMap, const QString &value)
    {
        if (value.isEmpty()) return;
        CContactItemField *field = CContactItemField::NewLC(KStorageTypeText, contentType);
        field->SetMapping(vcardMap);
        field->TextStorage()->SetTextL(toPtr(value));
        item.AddFieldL(*field);      // takes ownership
        CleanupStack::Pop(field);
    }
}
#endif

namespace
{
    bool isEmojiCodePoint(uint c)
    {
        return (c >= 0x1F000 && c <= 0x1FFFF) || (c >= 0x2600 && c <= 0x27BF)
            || (c >= 0x2B00 && c <= 0x2BFF) || (c >= 0xFE00 && c <= 0xFE0F)
            || c == 0x200D || c == 0x20E3;
    }

    /// The username in a t.me Web Address, or empty when the URL is something else. This is how
    /// a contact with no phone number can still be opened: "Add to local contacts" stores
    /// https://t.me/<name> and tapping the row resolves that name.
    QString usernameFromUrl(const QString &url)
    {
        QString u = url.trimmed();
        if (u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) u = u.mid(8);
        else if (u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) u = u.mid(7);
        if (u.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) u = u.mid(4);
        const int slash = u.indexOf(QLatin1Char('/'));
        if (slash < 0) return QString();
        const QString host = u.left(slash).toLower();
        if (host != QLatin1String("t.me") && host != QLatin1String("telegram.me")
            && host != QLatin1String("telegram.dog")) return QString();
        const QString name = u.mid(slash + 1).section(QLatin1Char('?'), 0, 0)
                              .section(QLatin1Char('#'), 0, 0).section(QLatin1Char('/'), 0, 0);
        if (name.isEmpty() || name.startsWith(QLatin1Char('+'))) return QString();
        for (int i = 0; i < name.size(); ++i) {
            const QChar ch = name.at(i);
            if (!ch.isLetterOrNumber() && ch != QLatin1Char('_')) return QString();
        }
        return name;
    }

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
    roles[UsernameRole] = "username";
    roles[DetailRole] = "detail";
    roles[TargetRole] = "target";
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
    case UsernameRole: return e.username;
    // What the row shows, and what gets resolved when it is tapped.
    case DetailRole: return e.username.isEmpty() ? e.number : (QLatin1Char('@') + e.username);
    case TargetRole: return e.username.isEmpty() ? e.number : (QLatin1Char('@') + e.username);
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
        if (needle.isEmpty() || e.name.toLower().contains(needle) || e.number.contains(needle)
            || e.username.toLower().contains(needle))
            m_view.append(e);
    }
    endResetModel();
    emit changed();
}

QString PhoneBook::plainName(const QString &text) const
{
    QVector<uint> in = text.toUcs4();
    QString out;
    for (int i = 0; i < in.size(); ++i)
        if (!isEmojiCodePoint(in.at(i))) out += QString::fromUcs4(&in[i], 1);
    return out.simplified();
}

bool PhoneBook::addContact(const QString &firstName, const QString &lastName,
                           const QString &phone, const QString &url)
{
    const QString first = plainName(firstName);
    const QString last = plainName(lastName);
    const QString num = phone.trimmed();
    const QString web = url.trimmed();
    if (first.isEmpty() && last.isEmpty() && num.isEmpty() && web.isEmpty()) {
        m_error = tr("Nothing to save.");
        emit changed();
        return false;
    }

#ifdef Q_OS_SYMBIAN
    CContactDatabase *db = 0;
    TRAPD(openErr, db = CContactDatabase::OpenL());
    if (openErr != KErrNone || !db) {
        m_error = tr("Could not open the phonebook (%1).").arg(openErr);
        emit changed();
        return false;
    }
    TRAPD(err, addContactL(db, first, last, num, web));
    delete db;
    if (err != KErrNone) {
        m_error = tr("Could not save the contact (%1).").arg(err);
        emit changed();
        return false;
    }
    reload();          // so the new entry is there the next time Contacts opens
    return true;
#else
    m_error = tr("The phone's address book is only available on the device.");
    emit changed();
    return false;
#endif
}

#ifdef Q_OS_SYMBIAN

void PhoneBook::addContactL(void *dbPtr, const QString &firstName, const QString &lastName,
                            const QString &phone, const QString &url)
{
    CContactDatabase *db = static_cast<CContactDatabase *>(dbPtr);
    CContactItem *item = CContactCard::NewLC();
    addTextFieldL(*item, KUidContactFieldGivenName, KUidContactFieldVCardMapUnusedN, firstName);
    addTextFieldL(*item, KUidContactFieldFamilyName, KUidContactFieldVCardMapUnusedN, lastName);
    // The mobile label is what the native Contacts application shows for this mapping.
    if (!phone.isEmpty()) {
        CContactItemField *f = CContactItemField::NewLC(KStorageTypeText, KUidContactFieldPhoneNumber);
        f->SetMapping(KUidContactFieldVCardMapTEL);
        f->AddFieldTypeL(KUidContactFieldVCardMapCELL);
        f->TextStorage()->SetTextL(toPtr(phone));
        item->AddFieldL(*f);
        CleanupStack::Pop(f);
    }
    addTextFieldL(*item, KUidContactFieldUrl, KUidContactFieldVCardMapURL, url);
    db->AddNewContactL(*item);
    CleanupStack::PopAndDestroy(item);
}

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
        QStringList urls;
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
            else if (type == KUidContactFieldUrl && !urls.contains(value))
                urls.append(value);
        }

        QString name = (given + QLatin1Char(' ') + family).simplified();
        if (name.isEmpty()) name = company;

        // A t.me Web Address names the person outright, so it gets its own row - this is what
        // makes a contact with no phone number usable.
        // A t.me Web Address names the person outright, so it gets its own row - this is what
        // makes a contact with no phone number usable.
        for (int k = 0; k < urls.size(); ++k) {
            const QString user = usernameFromUrl(urls.at(k));
            if (user.isEmpty()) continue;
            Entry e;
            e.username = user;
            e.name = name.isEmpty() ? (QLatin1Char('@') + user) : name;
            m_all.append(e);
        }
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
