// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The chat list: me and the connection in the header, then the chats newest first with the
// last message, unread badges and mute marks. Tap opens the chat; long-press for mute /
// clear. The + button finds someone by username, phone or name.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    Component { id: settingsPage; SettingsPage {} }
    Component { id: aboutPage; AboutPage {} }

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: Qt.quit() }
        ToolButton { iconSource: "toolbar-add"; onClicked: findDialog.open() }
        ToolButton { iconSource: "toolbar-refresh"; onClicked: app.connection == "offline" ? app.reconnect() : app.chats.refresh() }
        ToolButton { iconSource: "toolbar-menu"; onClicked: menu.open() }
    }

    Menu {
        id: menu
        MenuLayout {
            MenuItem {
                text: app.connection == "offline" ? qsTr("Connect") : qsTr("Disconnect")
                onClicked: app.connection == "offline" ? app.reconnect() : app.goOffline()
            }
            MenuItem { text: qsTr("Settings"); onClicked: pageStack.push(settingsPage) }
            MenuItem { text: qsTr("About"); onClicked: pageStack.push(aboutPage) }
            MenuItem { text: qsTr("Sign out"); onClicked: signOutDialog.open() }
            MenuItem { text: qsTr("Exit"); onClicked: Qt.quit() }
        }
    }

    QueryDialog {
        id: signOutDialog
        titleText: qsTr("Sign out")
        message: qsTr("Sign out? The session will be ended on Telegram and removed from this phone.")
        acceptButtonText: qsTr("Sign out")
        rejectButtonText: qsTr("Cancel")
        onAccepted: app.signOut()
    }

    CommonDialog {
        id: findDialog
        titleText: qsTr("New chat")
        buttonTexts: [qsTr("Find"), qsTr("Cancel")]
        content: Column {
            width: parent.width
            spacing: platformStyle.paddingMedium
            anchors { left: parent.left; right: parent.right; margins: platformStyle.paddingLarge }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                text: qsTr("@username, phone number, t.me link, or a contact's name")
            }
            TextField {
                id: findField
                width: parent.width
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                placeholderText: qsTr("@username or +phone")
            }
        }
        onButtonClicked: if (index == 0) app.findPeer(findField.text)
    }

    QueryDialog {
        id: clearDialog
        property string peerKey
        property string title
        titleText: qsTr("Clear history")
        message: qsTr("Delete all messages of \"%1\" on Telegram (for you)?").arg(title)
        acceptButtonText: qsTr("Delete")
        rejectButtonText: qsTr("Cancel")
        onAccepted: app.chats.clearHistory(peerKey)
    }

    ContextMenu {
        id: contextMenu
        property variant item
        MenuLayout {
            MenuItem {
                text: contextMenu.item && contextMenu.item.muted ? qsTr("Unmute") : qsTr("Mute")
                onClicked: app.chats.setMuted(contextMenu.item.peerKey, !contextMenu.item.muted)
            }
            MenuItem {
                text: qsTr("Clear history")
                onClicked: { clearDialog.peerKey = contextMenu.item.peerKey; clearDialog.title = contextMenu.item.title; clearDialog.open() }
            }
        }
    }

    // -- header: me and the connection --
    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Image {
            id: logo
            source: "qrc:/images/logo.png"
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
        }
        Column {
            anchors { left: logo.right; leftMargin: platformStyle.paddingLarge; right: busy.left; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: app.myName != "" ? app.myName : "Symbigram"; elide: Text.ElideRight; font.bold: true }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                color: app.connection == "online" ? "#8fd18f" : platformStyle.colorNormalMid
                text: app.connection == "online" ? (app.mySubtitle != "" ? app.mySubtitle : qsTr("online"))
                    : (app.connection == "connecting" ? qsTr("connecting...") : qsTr("offline"))
                elide: Text.ElideRight
            }
        }
        BusyIndicator {
            id: busy
            anchors { right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            running: app.connection == "connecting" || app.chats.loading
            visible: running
            width: visible ? platformStyle.graphicSizeSmall : 0
        }
    }

    // -- list --
    ListView {
        id: list
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        model: app.chats
        clip: true
        cacheBuffer: 600
        footer: Item {
            width: list.width
            height: app.chats.hasMore ? platformStyle.graphicSizeMedium + 2 * platformStyle.paddingLarge : 0
            Button {
                anchors.centerIn: parent
                visible: app.chats.hasMore
                text: app.chats.loading ? qsTr("loading...") : qsTr("Older chats")
                enabled: !app.chats.loading
                onClicked: app.chats.loadMore()
            }
        }

        delegate: ListItem {
            id: item
            subItemIndicator: false
            height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingLarge

            // avatar: initials on a colour that is stable per chat
            Rectangle {
                id: avatar
                anchors { left: item.paddingItem.left; verticalCenter: parent.verticalCenter }
                width: platformStyle.graphicSizeMedium
                height: platformStyle.graphicSizeMedium
                radius: width / 2
                color: model.color
                clip: true
                Label {
                    anchors.centerIn: parent
                    text: model.initials
                    color: "white"
                    font.bold: true
                    font.pixelSize: platformStyle.fontSizeMedium
                    visible: avatarImage.status != Image.Ready
                }
                Image {
                    id: avatarImage
                    anchors.fill: parent
                    source: model.avatar
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    asynchronous: true
                }
                Rectangle {
                    // presence dot for people who are online
                    anchors { right: parent.right; bottom: parent.bottom }
                    width: 12; height: 12; radius: 6
                    color: "#4ccb4c"
                    border.color: "#1c2a3a"
                    border.width: 2
                    visible: model.online
                }
            }
            Column {
                anchors {
                    left: avatar.right; leftMargin: platformStyle.paddingLarge
                    right: rightColumn.left; rightMargin: platformStyle.paddingSmall
                    verticalCenter: parent.verticalCenter
                }
                Row {
                    width: parent.width
                    spacing: platformStyle.paddingSmall
                    ListItemText {
                        id: titleText
                        width: Math.min(implicitWidth, parent.width - (mutedMark.visible ? mutedMark.width + platformStyle.paddingSmall : 0))
                        role: "Title"
                        text: model.title
                        elide: Text.ElideRight
                    }
                    Image {
                        id: mutedMark
                        source: "qrc:/images/muted.png"
                        visible: model.muted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                ListItemText {
                    width: parent.width
                    role: "SubTitle"
                    text: model.subtitle
                    elide: Text.ElideRight
                    visible: text != ""
                    color: model.typing ? "#8fd18f" : platformStyle.colorNormalMid
                }
            }
            Column {
                id: rightColumn
                anchors { right: item.paddingItem.right; verticalCenter: parent.verticalCenter }
                spacing: platformStyle.paddingSmall
                Label {
                    anchors.right: parent.right
                    text: model.timeText
                    font.pixelSize: platformStyle.fontSizeSmall * 0.85
                    color: platformStyle.colorNormalMid
                }
                Rectangle {
                    id: badge
                    anchors.right: parent.right
                    width: Math.max(badgeLabel.width + 12, 24)
                    height: 22
                    radius: 11
                    color: model.muted ? "#5a6674" : "#2f8fd8"
                    visible: model.unread > 0
                    Label {
                        id: badgeLabel
                        anchors.centerIn: parent
                        text: model.unread > 999 ? "999+" : model.unread
                        font.pixelSize: platformStyle.fontSizeSmall * 0.85
                        color: "white"
                    }
                }
                Image {
                    anchors.right: parent.right
                    source: "qrc:/images/pinned.png"
                    visible: model.pinned && model.unread == 0
                }
            }

            onClicked: window.openChat(model.peerKey)
            onPressAndHold: {
                contextMenu.item = app.chats.get(index)
                contextMenu.open()
            }
        }

        ScrollDecorator { flickableItem: list }
    }

    Label {
        anchors.centerIn: list
        width: parent.width - 2 * platformStyle.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: platformStyle.colorNormalMid
        visible: list.count == 0
        text: app.connection == "online" ? (app.chats.loading ? qsTr("Loading chats...") : qsTr("No chats yet. Tap + to find someone."))
              : (app.connection == "connecting" ? qsTr("Connecting...") : qsTr("Offline. Use the menu to connect."))
    }
}
