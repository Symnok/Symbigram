// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Review and edit a new phonebook entry before it is saved. Reached from a chat's menu with the
// Web Address already filled in as t.me/<username>, and the name guessed from the chat title.
//
// This is a Page rather than a dialog on purpose: a text field inside a CommonDialog has its
// cut/copy/paste bubble drawn behind the dialog on this platform, which makes pasting unusable.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    // Set by the caller through pageStack.push(newContactPage, { ... }).
    property string firstName: ""
    property string lastName: ""
    property string phone: ""
    property string url: ""

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
        ToolButton {
            text: qsTr("Save")
            enabled: firstField.text != "" || lastField.text != "" || phoneField.text != "" || urlField.text != ""
            onClicked: {
                app.addLocalContact(firstField.text, lastField.text, phoneField.text, urlField.text)
                pageStack.pop()
            }
        }
    }

    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Image {
            id: headingIcon
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            source: "qrc:/images/contacts.png"
            width: platformStyle.graphicSizeMedium
            height: platformStyle.graphicSizeMedium
            smooth: true
        }
        Label {
            anchors { left: headingIcon.right; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            text: qsTr("New contact")
            font.bold: true
            color: "white"
            elide: Text.ElideRight
        }
    }

    Flickable {
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        contentHeight: form.height + 2 * platformStyle.paddingLarge
        clip: true

        Column {
            id: form
            width: parent.width - 2 * platformStyle.paddingLarge
            x: platformStyle.paddingLarge
            y: platformStyle.paddingLarge
            spacing: platformStyle.paddingMedium

            Label { text: qsTr("First name"); color: platformStyle.colorNormalMid; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: firstField; width: parent.width; text: page.firstName }

            Label { text: qsTr("Last name"); color: platformStyle.colorNormalMid; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: lastField; width: parent.width; text: page.lastName }

            Label { text: qsTr("Phone (optional)"); color: platformStyle.colorNormalMid; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: phoneField; width: parent.width; text: page.phone; inputMethodHints: Qt.ImhDialableCharactersOnly }

            Label { text: qsTr("Web address"); color: platformStyle.colorNormalMid; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: urlField; width: parent.width; text: page.url; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Keeping the t.me web address lets Symbigram open this chat from Contacts even without a phone number. Any other details can be added afterwards in the phone's Contacts application.")
            }
        }
    }
}
