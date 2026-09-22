// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One conversation: the chat's avatar and presence in the header, messages oldest at the
// top (older pages on request), and the composer with an attach button. Long-press a bubble
// to copy, save an attachment, retry or delete. Tapping an image opens the viewer.
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

    Menu {
        id: attachMenu
        MenuLayout {
            MenuItem { text: qsTr("Image"); onClicked: app.attachFile(true) }
            MenuItem { text: qsTr("File"); onClicked: app.attachFile(false) }
        }
    }

    ContextMenu {
        id: contextMenu
        property int row: -1
        property variant item
        MenuLayout {
            MenuItem {
                text: qsTr("Save to phone")
                visible: contextMenu.item ? (contextMenu.item.mediaKind != "" && contextMenu.item.mediaKind != "sticker") : false
                onClicked: chat.saveMedia(contextMenu.row)
            }
            MenuItem {
                text: qsTr("Open")
                visible: contextMenu.item ? (contextMenu.item.mediaState == "ready" && contextMenu.item.mediaKind != "photo") : false
                onClicked: chat.openMedia(contextMenu.row)
            }
            MenuItem {
                text: qsTr("Copy text")
                visible: contextMenu.item ? contextMenu.item.body != "" : false
                onClicked: app.copyText(contextMenu.item.body)
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
                visible: contextMenu.row >= 0 ? chat.canDeleteForEveryone(contextMenu.row) : false
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
            clip: true
            Label { anchors.centerIn: parent; text: chat.initials; color: "white"; font.bold: true; visible: headerAvatar.status != Image.Ready }
            Image { id: headerAvatar; anchors.fill: parent; source: chat.avatar; fillMode: Image.PreserveAspectCrop; smooth: true }
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
            onOpenImage: viewer.show(path, row)
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
        ToolButton {
            id: attachButton
            visible: !chat.peerIsChannel
            anchors { left: parent.left; leftMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            iconSource: "toolbar-add"
            enabled: app.connection == "online"
            onClicked: attachMenu.open()
        }
        TextArea {
            id: composer
            visible: !chat.peerIsChannel
            anchors {
                left: attachButton.right; leftMargin: platformStyle.paddingSmall
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

    // -- full-screen image viewer --
    Rectangle {
        id: viewer
        anchors.fill: parent
        color: "black"
        visible: false
        z: 100
        property string path: ""
        property int row: -1
        function show(p, r) { path = p; row = r; visible = true }
        function hide() { visible = false; path = "" }

        Flickable {
            id: viewerFlick
            anchors.fill: parent
            contentWidth: viewerImage.width
            contentHeight: viewerImage.height
            clip: true
            Image {
                id: viewerImage
                source: viewer.path
                width: Math.max(viewer.width, sourceSize.width)
                height: Math.max(viewer.height, sourceSize.height)
                fillMode: Image.PreserveAspectFit
            }
        }
        BusyIndicator { anchors.centerIn: parent; running: viewerImage.status == Image.Loading; visible: running }
        MouseArea { anchors.fill: parent; onClicked: viewer.hide() }
        Row {
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: platformStyle.paddingLarge }
            spacing: platformStyle.paddingLarge
            Button {
                text: qsTr("Save")
                onClicked: if (viewer.row >= 0) chat.saveMedia(viewer.row)
            }
            Button { text: qsTr("Close"); onClicked: viewer.hide() }
        }
    }
}
