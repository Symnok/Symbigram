// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The proxy settings, shared by Settings and the login screen. One copy on purpose: the login
// screen used to carry its own SOCKS5-only version, which meant the MTProto fields were missing
// exactly where they are needed most - when Telegram is blocked and the app cannot connect.
//
// The content scrolls, so it fits a short screen (the E6 is 640x480) with the MTProto fields in
// place. `maxHeight` is the page's height; the dialog still shrinks to fit shorter content.
import QtQuick 1.1
import com.nokia.symbian 1.1

CommonDialog {
    id: dialog

    /// 0 = SOCKS5, 1 = MTProto. Held here rather than read from app.proxyKind, so the dialog can
    /// be switched back and forth before anything is saved.
    property int kind: 0
    /// Adds the third "Off" button, for the login screen where turning a bad proxy off matters.
    property bool showOff: false
    /// The height to cap the scrolling content at - normally the page's own height.
    property int maxHeight: 400

    titleText: qsTr("Proxy")
    buttonTexts: showOff ? [app.proxyEnabled ? qsTr("Save") : qsTr("Enable"), qsTr("Cancel"), qsTr("Off")]
                         : [qsTr("Save"), qsTr("Cancel")]

    content: Item {
        width: parent.width
        height: Math.min(proxyColumn.height + platformStyle.paddingLarge * 2,
                         dialog.maxHeight - platformStyle.graphicSizeLarge * 3)

        Flickable {
            id: proxyFlick
            anchors.fill: parent
            contentWidth: width
            contentHeight: proxyColumn.height + platformStyle.paddingLarge * 2
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: proxyColumn
                y: platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                width: proxyFlick.width - 2 * platformStyle.paddingLarge
                spacing: platformStyle.paddingMedium

                Label { text: qsTr("Type"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                ButtonRow {
                    width: parent.width
                    checkedButton: dialog.kind == 1 ? mtprotoKindButton : socksKindButton
                    Button { id: socksKindButton; text: qsTr("SOCKS5"); onClicked: dialog.kind = 0 }
                    Button { id: mtprotoKindButton; text: qsTr("MTProto"); onClicked: dialog.kind = 1 }
                }

                Label { text: qsTr("Server"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                TextField {
                    id: proxyHostField
                    width: parent.width
                    placeholderText: qsTr("host or IP")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
                Label { text: qsTr("Port"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                TextField {
                    id: proxyPortField
                    width: parent.width
                    placeholderText: dialog.kind == 1 ? "443" : "1080"
                    inputMethodHints: Qt.ImhDigitsOnly
                }

                // SOCKS5 credentials.
                Label { visible: dialog.kind == 0; text: qsTr("Username (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                TextField { visible: dialog.kind == 0; id: proxyUserField; width: parent.width; inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText }
                Label { visible: dialog.kind == 0; text: qsTr("Password (optional)"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                TextField { visible: dialog.kind == 0; id: proxyPassField; width: parent.width; echoMode: TextInput.Password }

                // MTProto secret.
                Label { visible: dialog.kind == 1; text: qsTr("Secret"); color: "white"; font.pixelSize: platformStyle.fontSizeSmall }
                TextField {
                    visible: dialog.kind == 1
                    id: proxySecretField
                    width: parent.width
                    placeholderText: qsTr("hex or base64, from the proxy link")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                }
                Label {
                    visible: dialog.kind == 1
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: platformStyle.colorNormalMid
                    font.pixelSize: platformStyle.fontSizeSmall
                    text: qsTr("Or paste a whole tg://proxy or t.me/proxy link into Secret and tap Use link.")
                }
                Button {
                    visible: dialog.kind == 1
                    width: parent.width
                    text: qsTr("Use link")
                    onClicked: if (app.applyProxyLink(proxySecretField.text)) { dialog.load(); dialog.close() }
                }
            }
        }

        ScrollDecorator { flickableItem: proxyFlick }
    }

    onButtonClicked: {
        if (index == 0) {
            if (dialog.kind == 1 && proxySecretField.text.indexOf("://") >= 0) {
                // A link left in the Secret field is almost certainly meant as one.
                if (app.applyProxyLink(proxySecretField.text)) return
            }
            app.saveProxy(true, proxyHostField.text, proxyPortField.text,
                          proxyUserField.text, proxyPassField.text,
                          dialog.kind, proxySecretField.text)
        } else if (dialog.showOff && index == 2) {
            app.setProxyEnabled(false)
        }
    }

    function load() {
        dialog.kind = app.proxyKind
        proxyHostField.text = app.proxyHost
        proxyPortField.text = app.proxyPort
        proxyUserField.text = app.proxyUser
        proxyPassField.text = app.proxyPass
        proxySecretField.text = app.proxySecret
    }
}
