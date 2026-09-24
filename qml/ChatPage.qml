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
            MenuItem { text: chat.hasOlder ? qsTr("Load older messages") : qsTr("Reload"); visible: !chat.isSecret; onClicked: chat.hasOlder ? chat.loadOlder() : reopen() }
            MenuItem { text: chat.peerMuted ? qsTr("Unmute") : qsTr("Mute"); visible: !chat.isSecret; onClicked: chat.setMuted(!chat.peerMuted) }
            MenuItem { text: qsTr("Mark as read"); visible: !chat.isSecret; onClicked: chat.markRead() }
            MenuItem {
                text: qsTr("Start secret chat")
                visible: !chat.isSecret && !chat.peerIsGroup && !chat.peerIsChannel && chat.peerKey != ""
                onClicked: app.startSecretChat(chat.peerKey)
            }
            MenuItem {
                text: qsTr("Verify encryption key")
                visible: chat.isSecret && chat.secretState == 2
                onClicked: { verifyDialog.hex = chat.secretKeyHex(); verifyDialog.open() }
            }
            MenuItem {
                text: qsTr("Self-destruct timer")
                visible: chat.isSecret && chat.secretState == 2
                onClicked: ttlDialog.open()
            }
            MenuItem {
                text: qsTr("Delete secret chat")
                visible: chat.isSecret
                onClicked: discardDialog.open()
            }
        }
    }

    function reopen() { var k = chat.peerKey; chat.close(); chat.open(k) }

    Connections {
        target: chat
        onMediaSaved: { savedDialog.path = path; savedDialog.open() }
    }

    QueryDialog {
        id: savedDialog
        property string path: ""
        titleText: qsTr("Saved")
        message: qsTr("File saved to: %1").arg(path)
        acceptButtonText: qsTr("OK")
    }

    QueryDialog {
        id: discardDialog
        titleText: qsTr("Delete secret chat")
        message: qsTr("End this secret chat? Its messages, which live only on this device, will be removed here.")
        acceptButtonText: qsTr("Delete")
        rejectButtonText: qsTr("Cancel")
        onAccepted: { chat.discardSecret(); chat.close(); pageStack.pop() }
    }

    SelectionDialog {
        id: ttlDialog
        property variant secsList: [0, 5, 30, 60, 3600, 86400, 604800]
        titleText: qsTr("Self-destruct timer")
        selectedIndex: -1
        model: [qsTr("Off"), qsTr("5 seconds"), qsTr("30 seconds"), qsTr("1 minute"), qsTr("1 hour"), qsTr("1 day"), qsTr("1 week")]
        onAccepted: if (selectedIndex >= 0) chat.setSecretTtl(secsList[selectedIndex])
    }

    CommonDialog {
        id: verifyDialog
        property string hex: ""
        titleText: qsTr("Encryption key")
        buttonTexts: [qsTr("Close")]
        content: Item {
            width: parent.width
            height: verifyCol.height + 2 * platformStyle.paddingLarge
            Column {
                id: verifyCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: platformStyle.paddingLarge }
                spacing: platformStyle.paddingMedium
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: platformStyle.colorNormalLight
                    text: qsTr("If this key matches on both phones, no one is intercepting the chat. Compare it with %1 in person or over a trusted channel.").arg(chat.title)
                }
                Label {
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    font.family: "Courier"
                    font.pixelSize: platformStyle.fontSizeMedium
                    horizontalAlignment: Text.AlignHCenter
                    text: verifyDialog.hex
                }
            }
        }
    }

    Menu {
        id: attachMenu
        MenuLayout {
            MenuItem { text: qsTr("Image"); onClicked: captionDialog.pick(true) }
            MenuItem { text: qsTr("File"); onClicked: captionDialog.pick(false) }
        }
    }

    // After a file is chosen: add/edit a caption (prefilled with whatever is in the composer),
    // then send. This lets you attach first and caption afterwards.
    CommonDialog {
        id: captionDialog
        property string path: ""
        property bool asPhoto: true
        titleText: asPhoto ? qsTr("Send image") : qsTr("Send file")
        buttonTexts: [qsTr("Send"), qsTr("Cancel")]
        function pick(photo) {
            var p = app.pickAttachment(photo)
            if (p == "") return
            path = p
            asPhoto = photo
            captionField.text = composer.text
            open()
        }
        content: Column {
            width: parent.width
            spacing: platformStyle.paddingMedium
            anchors { left: parent.left; right: parent.right; margins: platformStyle.paddingLarge }
            Label {
                width: parent.width
                text: captionDialog.path.split(/[\\/]/).pop()
                color: "white"; elide: Text.ElideMiddle
            }
            Label { text: qsTr("Caption (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: captionField
                width: parent.width
                placeholderText: qsTr("add a caption")
            }
        }
        onButtonClicked: {
            if (index == 0) { app.sendAttachment(captionDialog.path, captionDialog.asPhoto, captionField.text); composer.text = "" }
        }
    }

    ContextMenu {
        id: contextMenu
        property int row: -1
        property variant item
        MenuLayout {
            MenuItem {
                text: qsTr("Reply")
                visible: contextMenu.item ? (!contextMenu.item.pending && !contextMenu.item.service && !chat.isSecret && !chat.peerIsChannel) : false
                onClicked: { chat.startReply(contextMenu.row); composer.forceActiveFocus(); composer.openSoftwareInputPanel() }
            }
            MenuItem {
                text: qsTr("Edit")
                visible: contextMenu.row >= 0 ? chat.canEdit(contextMenu.row) : false
                onClicked: {
                    chat.startEdit(contextMenu.row)
                    composer.text = contextMenu.item.body
                    composer.forceActiveFocus()
                    composer.openSoftwareInputPanel()
                }
            }
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
            Row {
                width: parent.width
                spacing: platformStyle.paddingSmall
                Image {
                    source: "qrc:/images/lock.png"
                    visible: chat.isSecret
                    width: 16; height: 16; smooth: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width - (chat.isSecret ? 16 + platformStyle.paddingSmall : 0)
                    text: chat.title; elide: Text.ElideRight; font.bold: true
                    color: chat.isSecret ? "#8fd18f" : "white"
                }
            }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                text: app.connection == "online" ? chat.subtitle : (app.connection == "connecting" ? qsTr("connecting...") : qsTr("offline"))
                elide: Text.ElideRight
                color: chat.peerTyping || chat.subtitle == qsTr("online") ? "#8fd18f" : platformStyle.colorNormalMid
            }
        }
    }

    // -- secret-chat status / accept bar --
    Rectangle {
        id: secretBar
        anchors { top: heading.bottom; left: parent.left; right: parent.right }
        color: "#12321c"
        visible: chat.isSecret && chat.secretState != 2
        height: visible ? secretCol.height + 2 * platformStyle.paddingMedium : 0
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#2f6b3f"; visible: secretBar.visible }
        Column {
            id: secretCol
            anchors { left: parent.left; right: parent.right; margins: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            spacing: platformStyle.paddingMedium
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: "#bfe6c6"
                text: chat.secretState == 1
                      ? qsTr("%1 wants to start an end-to-end encrypted chat.").arg(chat.title)
                      : qsTr("Waiting for the other side to come online and accept...")
            }
            Row {
                spacing: platformStyle.paddingLarge
                visible: chat.secretState == 1
                Button { text: qsTr("Accept"); onClicked: chat.acceptSecret() }
                Button { text: qsTr("Decline"); onClicked: { chat.discardSecret(); chat.close(); pageStack.pop() } }
            }
            Button {
                text: qsTr("Cancel request")
                visible: chat.secretState == 0
                onClicked: { chat.discardSecret(); chat.close(); pageStack.pop() }
            }
        }
    }

    // -- messages --
    ListView {
        id: list
        anchors { top: secretBar.bottom; left: parent.left; right: parent.right; bottom: replyBar.top }
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

    // -- reply / edit bar (above the composer) --
    Item {
        id: replyBar
        property bool active: chat.replyToId > 0 || chat.editing
        anchors { left: parent.left; right: parent.right; bottom: composerRow.top }
        height: active ? replyContent.height + 2 * platformStyle.paddingSmall : 0
        visible: active
        clip: true
        Rectangle { anchors.fill: parent; color: "#1c2a3a" }
        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: 3; color: "#5b8fd0" }
        Row {
            id: replyContent
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: cancelReply.left; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            spacing: platformStyle.paddingSmall
            Column {
                width: parent.width
                Label { text: chat.editing ? qsTr("Editing message") : qsTr("Replying to"); color: "#5b8fd0"; font.pixelSize: platformStyle.fontSizeSmall }
                Label { width: parent.width; text: chat.editing ? qsTr("edit the text, then tap Save") : chat.replyToText; color: "white"; font.pixelSize: platformStyle.fontSizeSmall; elide: Text.ElideRight }
            }
        }
        Button {
            id: cancelReply
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: 56
            text: "✕"    // ✕
            onClicked: { if (chat.editing) { chat.cancelEdit(); composer.text = "" } else chat.cancelReply() }
        }
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
            visible: !chat.peerIsChannel && !chat.isSecret && !app.recording
            anchors { left: parent.left; leftMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            iconSource: "toolbar-add"
            enabled: app.connection == "online"
            onClicked: attachMenu.open()
        }
        TextArea {
            id: composer
            visible: !chat.peerIsChannel && !app.recording
            anchors {
                left: attachButton.visible ? attachButton.right : parent.left
                leftMargin: platformStyle.paddingSmall
                right: sendButton.left; rightMargin: platformStyle.paddingSmall
                verticalCenter: parent.verticalCenter
            }
            placeholderText: chat.isSecret ? qsTr("encrypted message") : qsTr("message")
            enabled: !chat.isSecret || chat.secretState == 2
            wrapMode: TextEdit.Wrap
            platformMaxImplicitHeight: 120
            onTextChanged: chat.composing(text)
        }
        Button {
            id: sendButton
            visible: !chat.peerIsChannel && !app.recording && composer.text.length > 0
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: Math.max(80, implicitWidth)
            text: chat.editing ? qsTr("Save") : qsTr("Send")
            enabled: composer.text.length > 0 && chat.peerKey != "" && app.connection == "online" && (!chat.isSecret || chat.secretState == 2)
            onClicked: {
                if (chat.editing) chat.commitEdit(composer.text)
                else chat.send(composer.text)
                composer.text = ""
            }
        }
        // a round record button when there is nothing typed
        Rectangle {
            id: micButton
            visible: !chat.peerIsChannel && !chat.isSecret && !app.recording && composer.text.length == 0
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: 56; height: 56; radius: 28
            color: micMouse.pressed ? "#3d5a80" : "#2f4a66"
            enabled: app.connection == "online"
            opacity: enabled ? 1 : 0.4
            Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 8; color: "#e04b4b" }
            MouseArea { id: micMouse; anchors.fill: parent; enabled: app.connection == "online"; onClicked: app.startRecording() }
        }
        // while recording: a pulsing dot, the elapsed time, cancel and send
        Item {
            id: recordingBar
            visible: app.recording
            anchors.fill: parent
            Rectangle {
                id: recDot
                anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
                width: 16; height: 16; radius: 8; color: "#e04b4b"
                SequentialAnimation on opacity {
                    running: app.recording; loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 550 }
                    NumberAnimation { to: 1.0; duration: 550 }
                }
            }
            Label {
                anchors { left: recDot.right; leftMargin: platformStyle.paddingMedium; verticalCenter: parent.verticalCenter }
                text: page.recText
                color: "white"
                font.pixelSize: platformStyle.fontSizeLarge
            }
            Button {
                id: recSend
                anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
                text: qsTr("Send")
                onClicked: app.stopRecording()
            }
            Button {
                anchors { right: recSend.left; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
                text: qsTr("Cancel")
                onClicked: app.cancelRecording()
            }
        }
    }

    property string recText: "0:00"
    Timer {
        interval: 250; repeat: true; running: app.recording
        onTriggered: {
            var ms = app.recordingMs()
            var t = Math.floor(ms / 1000)
            page.recText = Math.floor(t / 60) + ":" + (t % 60 < 10 ? "0" : "") + (t % 60)
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
