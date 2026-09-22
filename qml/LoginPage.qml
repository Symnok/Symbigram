// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// Signing in: the QR code another Telegram scans (Settings > Devices > Link Desktop
// Device), refreshed as tokens expire, and the two-step verification password when the
// account has one. There is no other way in - no phone number, no SMS code.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: Qt.quit() }
        ToolButton {
            iconSource: "toolbar-refresh"
            visible: app.connection == "offline"
            onClicked: app.reconnect()
        }
        ToolButton { iconSource: "toolbar-menu"; onClicked: loginMenu.open() }
    }

    Menu {
        id: loginMenu
        MenuLayout {
            MenuItem { text: qsTr("SOCKS5 proxy"); onClicked: { proxyDialog.load(); proxyDialog.open() } }
            MenuItem { text: qsTr("Reconnect"); onClicked: app.reconnect() }
        }
    }

    CommonDialog {
        id: proxyDialog
        titleText: qsTr("SOCKS5 proxy")
        buttonTexts: [app.proxyEnabled ? qsTr("Save") : qsTr("Enable"), qsTr("Cancel"), qsTr("Off")]
        content: Column {
            width: parent.width
            spacing: platformStyle.paddingMedium
            anchors { left: parent.left; right: parent.right; margins: platformStyle.paddingLarge }
            Label { text: qsTr("Server"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: proxyHostField; width: parent.width; placeholderText: qsTr("host or IP"); inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            Label { text: qsTr("Port"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: proxyPortField; width: parent.width; placeholderText: "1080"; inputMethodHints: Qt.ImhDigitsOnly }
            Label { text: qsTr("Username (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: proxyUserField; width: parent.width; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
            Label { text: qsTr("Password (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
            TextField { id: proxyPassField; width: parent.width; echoMode: TextInput.Password }
        }
        onButtonClicked: {
            if (index == 0) app.saveProxy(true, proxyHostField.text, proxyPortField.text, proxyUserField.text, proxyPassField.text)
            else if (index == 2) app.setProxyEnabled(false)
        }
        function load() {
            proxyHostField.text = app.proxyHost
            proxyPortField.text = app.proxyPort
            proxyUserField.text = app.proxyUser
            proxyPassField.text = app.proxyPass
        }
    }

    // seconds left on the code, for the countdown under it
    Timer {
        id: countdown
        interval: 1000
        repeat: true
        running: app.qrToken != ""
        triggeredOnStart: true
        onTriggered: page.secondsLeft = Math.max(0, app.qrExpires - Math.floor(Date.now() / 1000))
    }
    property int secondsLeft: 0

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 2 * platformStyle.paddingLarge
        flickableDirection: Flickable.VerticalFlick
        clip: true

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: platformStyle.paddingLarge }
            spacing: platformStyle.paddingMedium

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: platformStyle.paddingMedium
                Image { source: "qrc:/images/logo.png"; anchors.verticalCenter: parent.verticalCenter }
                Label {
                    text: "Symbigram"
                    font.pixelSize: platformStyle.fontSizeLarge * 1.5
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Label {
                text: qsTr("Telegram for Symbian")
                color: platformStyle.colorNormalMid
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Item { width: 1; height: platformStyle.paddingMedium }

            // -- the code --
            Rectangle {
                id: qrPlate
                anchors.horizontalCenter: parent.horizontalCenter
                width: 300
                height: 300
                radius: 6
                color: "white"
                visible: !app.passwordNeeded
                Image {
                    id: qrImage
                    anchors.centerIn: parent
                    // the token changes every ~30 s; a new source re-renders the code
                    source: app.qrToken != "" ? "image://qr/" + app.qrToken : ""
                    sourceSize.width: 290
                    smooth: false
                    cache: false
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    running: app.qrToken == "" && app.connection != "offline"
                    visible: running
                    width: platformStyle.graphicSizeLarge
                    height: platformStyle.graphicSizeLarge
                }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 2 * platformStyle.paddingLarge
                    spacing: platformStyle.paddingMedium
                    visible: app.qrToken == "" && app.connection == "offline"
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: "#404040"
                        text: app.proxyEnabled ? qsTr("No connection through the proxy. Check the proxy or tap Reconnect.")
                                               : qsTr("No connection. Tap Reconnect, or set up a proxy if Telegram is blocked.")
                    }
                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: app.proxyEnabled ? qsTr("Proxy: %1").arg(app.proxyHost != "" ? app.proxyHost : qsTr("on")) : qsTr("Set up proxy")
                        onClicked: { proxyDialog.load(); proxyDialog.open() }
                    }
                }
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: app.qrToken != "" && !app.passwordNeeded
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("The code renews in %1 s").arg(page.secondsLeft)
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                visible: !app.passwordNeeded
                text: qsTr("Open Telegram on a phone or PC where you are signed in: Settings > Devices > Link Desktop Device, and scan this code.")
            }

            // -- the password --
            Column {
                width: parent.width
                spacing: platformStyle.paddingMedium
                visible: app.passwordNeeded
                Label { text: qsTr("Two-step verification password"); font.pixelSize: platformStyle.fontSizeSmall }
                TextField {
                    id: passwordField
                    width: parent.width
                    echoMode: showPassword.checked ? TextInput.Normal : TextInput.Password
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    placeholderText: app.passwordHint != "" ? qsTr("hint: %1").arg(app.passwordHint) : qsTr("password")
                    enabled: !app.checkingPassword
                    Keys.onReturnPressed: page.submit()
                    Keys.onEnterPressed: page.submit()
                }
                CheckBox { id: showPassword; text: qsTr("show password") }
                Button {
                    width: parent.width
                    text: app.checkingPassword ? qsTr("checking...") : qsTr("sign in")
                    enabled: !app.checkingPassword && passwordField.text.length > 0
                    onClicked: page.submit()
                }
            }

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: app.loginStatus
                color: platformStyle.colorNormalMid
                font.pixelSize: platformStyle.fontSizeSmall
                visible: text != ""
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: app.loginError
                visible: text != ""
                color: "#ff6b6b"
                font.pixelSize: platformStyle.fontSizeSmall
            }

            Item { width: 1; height: platformStyle.paddingLarge }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Symbigram talks MTProto 2.0 straight to Telegram's servers: the encryption key is made on this phone and never leaves it. The session shows up under Devices in Telegram, where it can be ended at any time.")
            }
        }
    }

    function submit() {
        passwordField.closeSoftwareInputPanel()
        app.checkPassword(passwordField.text)
    }
}
