// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One conversation: the chat's avatar and presence in the header, messages oldest at the
// top (older pages on request), and the composer. Long-press a bubble to copy, retry or
// delete.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page
    property variant chat: app.chat

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: { chat.close(); pageStack.pop() } }
        ToolButton { iconSource: "toolbar-menu"; onClicked: menu.open() }
    }

    Menu {
        id: menu
        MenuLayout {
            MenuItem { text: chat.hasOlder ? qsTr("Load older messages") : qsTr("Reload"); onClicked: chat.hasOlder ? chat.loadOlder() : reopen() }
            MenuItem { text: chat.peerMuted ? qsTr("Unmute") : qsTr("Mute"); onClicked: chat.setMuted(!chat.peerMuted) }
            MenuItem { text: qsTr("Mark as read"); onClicked: chat.markRead() }
        }
    }

    function reopen() { var k = chat.peerKey; chat.close(); chat.open(k) }

    ContextMenu {
        id: contextMenu
        property int row: -1
        property variant item
        MenuLayout {
            MenuItem {
                text: qsTr("Copy text")
                onClicked: app.copyText(contextMenu.item.body != "" ? contextMenu.item.body : contextMenu.item.note)
            }
            MenuItem {
                text: qsTr("Retry")
                visible: contextMenu.item ? contextMenu.item.failed : false
                onClicked: chat.retry(contextMenu.row)
            }
            MenuItem {
                text: qsTr("Delete for me")
                visible: contextMenu.item ? !contextMenu.item.pending : false
                onClicked: chat.deleteMessage(contextMenu.row, false)
            }
            MenuItem {
                text: qsTr("Delete for everyone")
                visible: contextMenu.item ? (contextMenu.item.out && !contextMenu.item.pending && !chat.peerIsChannel) : false
                onClicked: chat.deleteMessage(contextMenu.row, true)
            }
        }
    }

    // -- header --
    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Rectangle {
            id: avatar
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            width: platformStyle.graphicSizeMedium
            height: platformStyle.graphicSizeMedium
            radius: width / 2
            color: chat.color != "" ? chat.color : "#3d5a80"
            Label { anchors.centerIn: parent; text: chat.initials; color: "white"; font.bold: true }
        }
        Column {
            anchors { left: avatar.right; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: chat.title; elide: Text.ElideRight; font.bold: true }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                text: app.connection == "online" ? chat.subtitle : (app.connection == "connecting" ? qsTr("connecting...") : qsTr("offline"))
                elide: Text.ElideRight
                color: chat.peerTyping || chat.subtitle == qsTr("online") ? "#8fd18f" : platformStyle.colorNormalMid
            }
        }
    }

    // -- messages --
    ListView {
        id: list
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: composerRow.top }
        model: chat
        clip: true
        spacing: platformStyle.paddingSmall
        cacheBuffer: 800

        header: Item {
            width: list.width
            height: chat.hasOlder || chat.loading ? platformStyle.graphicSizeMedium + platformStyle.paddingLarge : 0
            Button {
                anchors.centerIn: parent
                visible: chat.hasOlder && !chat.loading
                text: qsTr("Older messages")
                onClicked: chat.loadOlder()
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: chat.loading
                visible: running
            }
        }

        delegate: MessageDelegate {
            width: list.width
            onPressAndHold: {
                contextMenu.row = index
                contextMenu.item = chat.get(index)
                contextMenu.open()
            }
        }

        ScrollDecorator { flickableItem: list }

        function scrollToEnd() {
            if (count > 0) positionViewAtEnd()
        }
        Component.onCompleted: scrollToEnd()
    }

    Connections {
        target: chat
        onMessageAppended: list.scrollToEnd()
        onOlderPrepended: list.positionViewAtIndex(count, ListView.Beginning)
        onChatChanged: list.scrollToEnd()
    }

    Label {
        anchors.centerIn: list
        width: parent.width - 2 * platformStyle.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: platformStyle.colorNormalMid
        visible: list.count == 0 && !chat.loading
        text: chat.error != "" ? qsTr("Could not load the messages: %1").arg(chat.error) : qsTr("No messages yet.")
    }

    // -- composer --
    Item {
        id: composerRow
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: chat.peerIsChannel ? channelNote.height + 2 * platformStyle.paddingMedium : composer.height + 2 * platformStyle.paddingSmall

        Label {
            id: channelNote
            anchors.centerIn: parent
            visible: chat.peerIsChannel
            color: platformStyle.colorNormalMid
            font.pixelSize: platformStyle.fontSizeSmall
            text: qsTr("channel - only its admins can post")
        }
        TextArea {
            id: composer
            visible: !chat.peerIsChannel
            anchors {
                left: parent.left; leftMargin: platformStyle.paddingSmall
                right: sendButton.left; rightMargin: platformStyle.paddingSmall
                verticalCenter: parent.verticalCenter
            }
            placeholderText: qsTr("message")
            wrapMode: TextEdit.Wrap
            platformMaxImplicitHeight: 120
            onTextChanged: chat.composing(text)
        }
        Button {
            id: sendButton
            visible: !chat.peerIsChannel
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: Math.max(80, implicitWidth)
            text: qsTr("Send")
            enabled: composer.text.length > 0 && chat.peerKey != "" && app.connection == "online"
            onClicked: {
                chat.send(composer.text)
                composer.text = ""
            }
        }
    }
}
