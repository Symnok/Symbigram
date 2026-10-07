// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The phone's own address book. Tapping someone looks that one number up on Telegram and opens
// the chat. The address book itself is never uploaded - only the number you tap is sent, and
// only to resolve it.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
        ToolButton { iconSource: "toolbar-refresh"; onClicked: app.contacts.reload() }
    }

    Timer { id: searchTimer; interval: 250; onTriggered: app.contacts.setFilter(searchField.text) }

    // The phonebook model outlives this page, so a filter typed last time would still be applied
    // while the (recreated) search box looks empty - showing only the contact searched for before.
    // Start every visit from the full list.
    Component.onCompleted: {
        searchField.text = ""
        app.contacts.setFilter("")
    }

    // Same header shape and height as a chat, so the pages line up.
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
        Column {
            anchors { left: headingIcon.right; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: qsTr("Contacts"); font.bold: true; color: "white"; elide: Text.ElideRight }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: app.contacts.loading ? qsTr("reading the phonebook...") : qsTr("%1 numbers").arg(app.contacts.count)
                elide: Text.ElideRight
            }
        }
    }

    TextField {
        id: searchField
        anchors { top: heading.bottom; left: parent.left; right: parent.right; margins: platformStyle.paddingSmall }
        placeholderText: qsTr("search contacts")
        inputMethodHints: Qt.ImhNoPredictiveText
        onTextChanged: searchTimer.restart()
    }

    ListView {
        id: list
        anchors { top: searchField.bottom; topMargin: platformStyle.paddingSmall; left: parent.left; right: parent.right; bottom: parent.bottom }
        model: app.contacts
        clip: true

        delegate: ListItem {
            id: item
            subItemIndicator: true
            Item {
                anchors.fill: item.paddingItem
                Rectangle {
                    id: avatar
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: platformStyle.graphicSizeSmall
                    height: platformStyle.graphicSizeSmall
                    radius: width / 2
                    color: model.color
                    Label { anchors.centerIn: parent; text: model.initials; color: "white"; font.bold: true }
                }
                Column {
                    anchors { left: avatar.right; leftMargin: platformStyle.paddingMedium; right: parent.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: model.name }
                    ListItemText { width: parent.width; role: "SubTitle"; text: model.detail }
                }
            }
            // A Web Address entry resolves its @name; otherwise only this one number is sent,
            // and only to look it up.
            onClicked: app.findPeer(model.target)
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: app.contacts.loading
        visible: running
        width: platformStyle.graphicSizeLarge
        height: platformStyle.graphicSizeLarge
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * platformStyle.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: platformStyle.colorNormalMid
        visible: !app.contacts.loading && app.contacts.count == 0
        text: app.contacts.error != "" ? app.contacts.error : qsTr("No contacts found.")
    }
}
