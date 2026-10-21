// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The proxy settings - SOCKS5 or MTProto - shared by Settings and the login screen. The login
// screen is where a blocked user ends up, so it must offer exactly the same options.
//
// This is a Page rather than a CommonDialog on purpose, for the same reason as NewContactPage: a
// text field inside a dialog has its cut/copy/paste bubble drawn BEHIND the dialog on this
// platform, so pasting is impossible - and a proxy secret is 32+ characters that nobody wants to
// type by hand. Being a page also means the virtual keyboard is handled by the platform instead
// of by arithmetic against a shrinking dialog.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    /// 0 = SOCKS5, 1 = MTProto. Local until saved, so the type can be switched back and forth.
    property int kind: 0
    /// The login screen wants a way to switch a bad proxy off; Settings has its own switch.
    property bool showOff: false

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
        ToolButton {
            text: app.proxyEnabled ? qsTr("Save") : qsTr("Enable")
            onClicked: page.save()
        }
        ToolButton {
            text: qsTr("Off")
            visible: page.showOff && app.proxyEnabled
            onClicked: { app.setProxyEnabled(false); pageStack.pop() }
        }
    }

    Component.onCompleted: {
        page.kind = app.proxyKind
        hostField.text = app.proxyHost
        portField.text = app.proxyPort
        userField.text = app.proxyUser
        passField.text = app.proxyPass
        secretField.text = app.proxySecret
    }

    function save() {
        if (page.kind == 1 && secretField.text.indexOf("://") >= 0) {
            // A whole link left in the Secret field is almost certainly meant as one.
            if (app.applyProxyLink(secretField.text)) { pageStack.pop(); return }
        }
        app.saveProxy(true, hostField.text, portField.text, userField.text, passField.text,
                      page.kind, secretField.text)
        pageStack.pop()
    }

    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Column {
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: qsTr("Proxy"); font.bold: true; color: "white"; elide: Text.ElideRight }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: app.proxyEnabled ? qsTr("on - %1").arg(app.proxyHost) : qsTr("off")
                elide: Text.ElideRight
            }
        }
    }

    Flickable {
        id: flick
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
                  margins: platformStyle.paddingLarge }
        contentWidth: width
        contentHeight: col.height
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        /// Keeps the field being typed into above the keyboard.
        function ensureVisible(item) {
            if (!item) return
            var top = item.mapToItem(col, 0, 0).y
            var bottom = top + item.height
            if (top < contentY) contentY = top
            else if (bottom > contentY + height) contentY = bottom - height
        }

        Column {
            id: col
            width: flick.width
            spacing: platformStyle.paddingMedium

            Label { text: qsTr("Type"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            ButtonRow {
                width: parent.width
                checkedButton: page.kind == 1 ? mtprotoButton : socksButton
                Button { id: socksButton; text: qsTr("SOCKS5"); onClicked: page.kind = 0 }
                Button { id: mtprotoButton; text: qsTr("MTProto"); onClicked: page.kind = 1 }
            }

            Label { text: qsTr("Server"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: hostField
                width: parent.width
                placeholderText: qsTr("host or IP")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onActiveFocusChanged: if (activeFocus) flick.ensureVisible(this)
            }

            Label { text: qsTr("Port"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: portField
                width: parent.width
                placeholderText: page.kind == 1 ? "443" : "1080"
                inputMethodHints: Qt.ImhDigitsOnly
                onActiveFocusChanged: if (activeFocus) flick.ensureVisible(this)
            }

            // SOCKS5 credentials.
            Label { visible: page.kind == 0; text: qsTr("Username (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: userField
                visible: page.kind == 0
                width: parent.width
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onActiveFocusChanged: if (activeFocus) flick.ensureVisible(this)
            }
            Label { visible: page.kind == 0; text: qsTr("Password (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: passField
                visible: page.kind == 0
                width: parent.width
                echoMode: TextInput.Password
                onActiveFocusChanged: if (activeFocus) flick.ensureVisible(this)
            }

            // MTProto secret.
            Label { visible: page.kind == 1; text: qsTr("Secret"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField {
                id: secretField
                visible: page.kind == 1
                width: parent.width
                placeholderText: qsTr("hex or base64, from the proxy link")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onActiveFocusChanged: if (activeFocus) flick.ensureVisible(this)
            }
            Row {
                visible: page.kind == 1
                width: parent.width
                spacing: platformStyle.paddingMedium
                // An explicit button as well as the platform's paste bubble: pasting a whole
                // proxy link is the usual way to set this up, and it should not depend on
                // finding a long-press bubble.
                Button {
                    width: (parent.width - platformStyle.paddingMedium) / 2
                    text: qsTr("Paste")
                    onClicked: secretField.text = app.clipboardText()
                }
                Button {
                    width: (parent.width - platformStyle.paddingMedium) / 2
                    text: qsTr("Use link")
                    onClicked: if (app.applyProxyLink(secretField.text)) pageStack.pop()
                }
            }
            Label {
                visible: page.kind == 1
                width: parent.width
                wrapMode: Text.WordWrap
                color: platformStyle.colorNormalMid
                font.pixelSize: platformStyle.fontSizeSmall
                text: qsTr("Paste a whole tg://proxy or t.me/proxy link and tap Use link, or type the secret on its own and tap Save.")
            }
        }
    }

    ScrollDecorator { flickableItem: flick }
}
