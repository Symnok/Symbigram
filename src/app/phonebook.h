// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The phone's own address book, read-only, for starting a chat with someone already in it.
// One row per phone number (a contact with a mobile and a work number appears twice), so the
// number that gets looked up is always the one the user tapped.
//
// This reads the phonebook through the native CContactDatabase (cntmodel.lib), NOT Qt Mobility's
// QContactManager: QContactManager was tried in GContactsSync-Symbian and misbehaved badly on real
// Symbian hardware, so that route is deliberately avoided here too.
//
// Nothing is ever uploaded: picking a contact resolves that single number through
// contacts.resolvePhone. The address book is never sent to Telegram (contacts.importContacts,
// which would do exactly that, is not used anywhere in this app).
#ifndef PHONEBOOK_H
#define PHONEBOOK_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

class PhoneBook : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        NumberRole,
        UsernameRole,    // from a t.me Web Address on the contact, without the @
        DetailRole,      // what the row shows: "@name" or the phone number
        TargetRole,      // what to hand to findPeer(): "@name" or the phone number
        InitialsRole,
        ColorRole
    };

    explicit PhoneBook(QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    bool loading() const { return m_loading; }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }

    /// Reads the phonebook. Does nothing if it has already been read; reload() forces it again.
    Q_INVOKABLE void load();
    Q_INVOKABLE void reload();
    /// Narrows the list to names or numbers containing text (empty shows everything).
    Q_INVOKABLE void setFilter(const QString &text);
    /// Creates a phonebook entry. Any field may be empty; a t.me Web Address is what lets the
    /// contact be opened later when no phone number is known. Returns false and sets error().
    Q_INVOKABLE bool addContact(const QString &firstName, const QString &lastName,
                                const QString &phone, const QString &url);
    /// A name the phonebook can actually render: emoji are dropped, since the phone font has no
    /// glyphs for them and they would be stored as empty boxes.
    Q_INVOKABLE QString plainName(const QString &text) const;

signals:
    void changed();

private:
    struct Entry
    {
        QString name;
        QString number;     // empty for a Web Address entry
        QString username;   // empty for a plain phone entry
    };

    void readAll();        // fills m_all; platform specific
#ifdef Q_OS_SYMBIAN
    void readContactsL(void *db);   // CContactDatabase*; separate so TRAPD gets one statement
    void addContactL(void *db, const QString &firstName, const QString &lastName,
                     const QString &phone, const QString &url);
#endif
    void applyFilter();

    QList<Entry> m_all;
    QList<Entry> m_view;
    QString m_filter;
    bool m_loaded;
    bool m_loading;
    QString m_error;
};

#endif // PHONEBOOK_H
