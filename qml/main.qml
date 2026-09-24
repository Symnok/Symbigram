// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The window: a page stack that follows app.state (starting -> login -> ready) and the
// notice banner every page shares.
import QtQuick 1.1
import com.nokia.symbian 1.1
import com.nokia.extras 1.1

PageStackWindow {
    id: window
    showStatusBar: true
    showToolBar: true
    platformSoftwareInputPanelEnabled: true
    initialPage: startPage

    Page {
        id: startPage
        BusyIndicator {
            anchors.centerIn: parent
            running: true
            width: platformStyle.graphicSizeLarge
            height: platformStyle.graphicSizeLarge
        }
        Label {
            anchors { top: parent.top; topMargin: parent.height / 4; horizontalCenter: parent.horizontalCenter }
            text: "Symbigram"
            font.pixelSize: platformStyle.fontSizeLarge * 1.5
        }
        tools: ToolBarLayout {
            ToolButton { iconSource: "toolbar-back"; onClicked: Qt.quit() }
        }
    }

    Component { id: loginPage; LoginPage {} }
    Component { id: chatsPage; ChatsPage {} }
    Component { id: chatPage; ChatPage {} }
    Component { id: settingsPage; SettingsPage {} }
    Component { id: aboutPage; AboutPage {} }

    function route() {
        if (app.state == "login") {
            pageStack.clear()
            pageStack.push(loginPage)
        } else if (app.state == "ready") {
            if (pageStack.depth == 0 || pageStack.currentPage != chatsPage) {
                pageStack.clear()
                pageStack.push(chatsPage)
            }
        }
    }

    function openChat(peerKey) {
        app.chat.open(peerKey)
        pageStack.push(chatPage)
    }

    Connections {
        target: app
        onStateChanged: route()
        onNoticeChanged: {
            if (app.notice != "") {
                banner.text = app.notice
                banner.open()
            }
        }
        onPeerFound: {
            if (pageStack.currentPage != chatsPage && app.chat.peerKey != "") pageStack.pop()
            window.openChat(peerKey)
        }
        onOpenChatRequested: {
            if (app.state != "ready") return
            if (pageStack.currentPage != chatsPage && app.chat.peerKey != "") pageStack.pop()
            window.openChat(peerKey)
        }
    }

    Component.onCompleted: route()

    // Desktop testing (SGM_SHOT_DIR): screenshots of the pages.
    Timer {
        id: autotest
        property int step: 0
        interval: 2000
        repeat: true
        running: app.autotest
        onTriggered: {
            if (app.state == "login") {
                // the login page alone: nobody scans during a test run
                if (app.qrToken != "" && step == 0) { app.takeScreenshot("login"); step = 1 }
                else if (step == 1) Qt.quit()
                return
            }
            if (app.connection != "online") return
            step++
            if (step == 2) app.takeScreenshot("chats")
            else if (app.autotestFolder) {
                if (step == 3 && app.chats.folderNames.length > 2) app.chats.selectFolder(1)
                else if (step == 4) { app.takeScreenshot("folder"); app.chats.selectFolder(0) }
                else if (step == 5) { app.takeScreenshot("folder-back"); Qt.quit() }
            }
            else if (step == 3) { var pk = app.autotestPeer; if (pk != "") window.openChat(pk); else if (app.chats.count > 0) window.openChat(app.chats.get(0).peerKey) }
            else if (step == 8) app.takeScreenshot("chat")
            else if (step == 9) { app.chat.close(); pageStack.pop() }
            else if (step == 10) pageStack.push(settingsPage)
            else if (step == 11) app.takeScreenshot("settings")
            else if (step == 12) { pageStack.pop(); pageStack.push(aboutPage) }
            else if (step == 13) app.takeScreenshot("about")
            else if (step == 14) Qt.quit()
        }
    }

    InfoBanner {
        id: banner
        timeout: 4000
        onClicked: app.clearNotice()
    }
}
