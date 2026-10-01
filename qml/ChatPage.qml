// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One conversation: the chat's avatar and presence in the header, messages oldest at the
// top (older pages on request), and the composer with an attach button. Long-press a bubble
// to copy, save an attachment, retry or delete. Tapping an image opens the viewer.
import QtQuick 1.1
import com.nokia.symbian 1.1
import com.nokia.extras 1.1

Page {
    id: page
    property variant chat: app.chat

    // A file staged for sending: the composer becomes its caption. Kept out of a dialog so the
    // text field's own copy/paste bubble is not hidden behind one.
    property string pendingFile: ""
    property bool pendingPhoto: true

    function pickFile(photo) {
        var p = app.pickAttachment(photo)
        if (p == "") return
        page.pendingFile = p
        page.pendingPhoto = photo
        composer.forceActiveFocus()
        composer.openSoftwareInputPanel()
    }

    // Send the composed text, or the staged file with the composer as its caption. Shared by the
    // Send button and the keyboard Enter key (E6/E7 etc.: Enter sends instead of a newline).
    function sendMessage() {
        if (chat.peerKey == "" || app.connection != "online") return
        if (chat.isSecret && chat.secretState != 2) return
        if (page.pendingFile != "") {
            app.sendAttachment(page.pendingFile, page.pendingPhoto, composer.text)
            page.pendingFile = ""
            composer.text = ""
            return
        }
        if (composer.text.length == 0) return
        if (chat.editing) chat.commitEdit(composer.text)
        else chat.send(composer.text)
        composer.text = ""
    }

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
        onForwarded: { forwardedBanner.text = qsTr("Forwarded to %1").arg(toTitle); forwardedBanner.open() }
    }

    InfoBanner { id: forwardedBanner; timeout: 3000 }

    // Pick a chat or person to forward the selected message to. Existing chats are matched locally
    // as you type; other people are searched on the server and appended. Tapping a row forwards.
    Timer { id: forwardSearchTimer; interval: 350; onTriggered: app.searchPeers(forwardSearch.text) }
    CommonDialog {
        id: forwardDialog
        property int row: -1
        titleText: qsTr("Forward to")
        buttonTexts: [qsTr("Cancel")]
        content: Item {
            width: parent.width
            height: 340
            Column {
                anchors.fill: parent
                spacing: platformStyle.paddingSmall
                TextField {
                    id: forwardSearch
                    width: parent.width
                    placeholderText: qsTr("search chats and people")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onTextChanged: forwardSearchTimer.restart()
                }
                ListView {
                    id: forwardList
                    width: parent.width
                    height: parent.height - forwardSearch.height - platformStyle.paddingSmall
                    clip: true
                    model: app.peerSearchResults
                    delegate: Item {
                        width: forwardList.width
                        height: (typeof privateStyle != "undefined") ? privateStyle.menuItemHeight : 56
                        Rectangle { anchors.fill: parent; color: fwdMouse.pressed ? "#3d5a80" : "transparent" }
                        Column {
                            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
                            Label { width: parent.width; text: modelData.title; color: "white"; elide: Text.ElideRight }
                            Label {
                                width: parent.width
                                visible: !modelData.local
                                text: qsTr("not in your chats")
                                color: platformStyle.colorNormalMid
                                font.pixelSize: platformStyle.fontSizeSmall
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: fwdMouse
                            anchors.fill: parent
                            onClicked: { var k = modelData.peerKey; forwardDialog.close(); chat.forwardTo(forwardDialog.row, k) }
                        }
                    }
                    ScrollDecorator { flickableItem: forwardList }
                }
            }
        }
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
            MenuItem { text: qsTr("Image"); onClicked: page.pickFile(true) }
            MenuItem { text: qsTr("File"); onClicked: page.pickFile(false) }
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
                text: qsTr("Forward")
                visible: contextMenu.item ? (!contextMenu.item.pending && !contextMenu.item.service && !chat.isSecret) : false
                onClicked: { forwardDialog.row = contextMenu.row; forwardSearch.text = ""; app.searchPeers(""); forwardDialog.open() }
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
                text: qsTr("Copy link")
                visible: contextMenu.row >= 0 ? chat.messageLink(contextMenu.row) != "" : false
                onClicked: app.copyText(chat.messageLink(contextMenu.row))
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
        onChatChanged: { page.pendingFile = ""; list.scrollToEnd() }
        // First page in: jump to the first unread message, or the end when all is read.
        onInitialLoaded: {
            if (firstUnreadRow >= 0 && firstUnreadRow < list.count)
                list.positionViewAtIndex(firstUnreadRow, ListView.Beginning)
            else
                list.scrollToEnd()
        }
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

    // -- staged-file bar (above the composer): shows the file whose caption you are typing --
    Item {
        id: attachmentBar
        property bool active: page.pendingFile != ""
        anchors { left: parent.left; right: parent.right; bottom: composerRow.top }
        height: active ? attachContent.height + 2 * platformStyle.paddingSmall : 0
        visible: active
        clip: true
        Rectangle { anchors.fill: parent; color: "#173021" }
        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: 3; color: "#5ab943" }
        Row {
            id: attachContent
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: cancelAttach.left; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            Column {
                width: parent.width
                Label { text: page.pendingPhoto ? qsTr("Image") : qsTr("File"); color: "#5ab943"; font.pixelSize: platformStyle.fontSizeSmall }
                Label { width: parent.width; text: page.pendingFile.split(/[\\/]/).pop(); color: "white"; font.pixelSize: platformStyle.fontSizeSmall; elide: Text.ElideMiddle }
            }
        }
        Button {
            id: cancelAttach
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: 56
            text: "X"
            onClicked: page.pendingFile = ""
        }
    }

    // -- reply / edit bar (above the composer, or the staged-file bar) --
    Item {
        id: replyBar
        property bool active: chat.replyToId > 0 || chat.editing
        anchors { left: parent.left; right: parent.right; bottom: attachmentBar.top }
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
            text: "X"    // ✕
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
            placeholderText: page.pendingFile != "" ? qsTr("add a caption") : (chat.isSecret ? qsTr("encrypted message") : qsTr("message"))
            enabled: !chat.isSecret || chat.secretState == 2
            wrapMode: TextEdit.Wrap
            platformMaxImplicitHeight: 120
            onTextChanged: chat.composing(text)
            // Hardware Enter sends the message (E6/E7 etc.) rather than inserting a newline.
            Keys.onReturnPressed: { page.sendMessage(); event.accepted = true }
            Keys.onEnterPressed: { page.sendMessage(); event.accepted = true }
        }
        Button {
            id: sendButton
            visible: !chat.peerIsChannel && !app.recording && (composer.text.length > 0 || page.pendingFile != "")
            anchors { right: parent.right; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
            width: Math.max(80, implicitWidth)
            text: chat.editing ? qsTr("Save") : qsTr("Send")
            enabled: (composer.text.length > 0 || page.pendingFile != "") && chat.peerKey != "" && app.connection == "online" && (!chat.isSecret || chat.secretState == 2)
            onClicked: page.sendMessage()
        }
        // a round record button when there is nothing typed and no file staged
        Rectangle {
            id: micButton
            visible: !chat.peerIsChannel && !chat.isSecret && !app.recording && composer.text.length == 0 && page.pendingFile == ""
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
    // Opens fit-to-screen (the whole image visible). Double-tap toggles a zoom that can be panned;
    // a single tap zooms back out, or closes when already fit. The tap MouseArea lives INSIDE the
    // Flickable so the Flickable can take over drags for panning (a MouseArea on top would eat them).
    Rectangle {
        id: viewer
        anchors.fill: parent
        color: "black"
        visible: false
        z: 100
        property string path: ""
        property int row: -1
        property bool zoomed: false
        function show(p, r) { path = p; row = r; zoomed = false; visible = true }
        function hide() { visible = false; path = ""; zoomed = false }

        Flickable {
            id: viewerFlick
            anchors.fill: parent
            contentWidth: Math.max(width, viewerImage.width)
            contentHeight: Math.max(height, viewerImage.height)
            clip: true
            interactive: viewer.zoomed
            Image {
                id: viewerImage
                source: viewer.path
                // fit: scale the whole image into the screen. zoomed: 2.5x that (at least 1:1 pixels)
                // so it overflows the viewport and the Flickable can pan it.
                property real fitScale: (sourceSize.width > 0 && sourceSize.height > 0)
                        ? Math.min(viewerFlick.width / sourceSize.width, viewerFlick.height / sourceSize.height) : 1
                property real scaleF: viewer.zoomed ? Math.max(fitScale * 2.5, 1) : fitScale
                width: sourceSize.width * scaleF
                height: sourceSize.height * scaleF
                x: Math.max(0, (viewerFlick.contentWidth - width) / 2)
                y: Math.max(0, (viewerFlick.contentHeight - height) / 2)
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: true
            }
            MouseArea {
                anchors.fill: parent
                onClicked: viewerTap.restart()
                onDoubleClicked: { viewerTap.stop(); viewer.zoomed = !viewer.zoomed }
            }
        }
        // Distinguish a single tap from the first tap of a double-tap.
        Timer { id: viewerTap; interval: 250; onTriggered: { if (viewer.zoomed) viewer.zoomed = false; else viewer.hide() } }
        BusyIndicator { anchors.centerIn: parent; running: viewerImage.status == Image.Loading; visible: running }
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
