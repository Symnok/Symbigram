// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.height + 2 * platformStyle.paddingLarge
        clip: true

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: platformStyle.paddingLarge }
            spacing: platformStyle.paddingMedium

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: platformStyle.paddingMedium
                Image { source: "qrc:/images/logo.png"; anchors.verticalCenter: parent.verticalCenter }
                Label { text: "Symbigram"; font.pixelSize: platformStyle.fontSizeLarge * 1.5; anchors.verticalCenter: parent.verticalCenter }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("version %1").arg(app.version)
                color: platformStyle.colorNormalMid
            }
            Item { width: 1; height: platformStyle.paddingLarge }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTr("A Telegram client for Symbian Anna/Belle, ported from Lumigram/LumigramPlus for Windows Phone 8.1. It speaks MTProto 2.0 directly to Telegram - no bridge, no proxy; the encryption key is made on the phone.")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Sign-in is by QR code only (scan from a signed-in Telegram, plus the two-step password if set). Text messages, groups and channels; attachments are shown as notes, not downloaded.")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: "GPL-3.0-or-later."
            }
            Item { width: 1; height: platformStyle.paddingLarge }
            Label { text: qsTr("Log"); font.bold: true }
            Label {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                font.pixelSize: platformStyle.fontSizeSmall * 0.85
                font.family: "monospace"
                color: platformStyle.colorNormalMid
                text: app.logTail
            }
            Button {
                width: parent.width
                text: qsTr("Copy log")
                onClicked: app.copyText(app.logTail)
            }
        }
    }
}
