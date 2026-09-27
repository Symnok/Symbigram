// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The Notifications sub-screen of Settings: three independent alert channels (popup, sound,
// vibration), each Off / First message only / Every message, plus the groups-and-channels
// switch. Reached from the "Notifications" entry on the main Settings page.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    SelectionDialog {
        id: popupModeDialog
        titleText: qsTr("Popup for new messages")
        model: app.popupModeNames()
        onAccepted: if (selectedIndex >= 0) app.popupMode = selectedIndex
    }

    SelectionDialog {
        id: soundModeDialog
        titleText: qsTr("Sound")
        model: app.popupModeNames()
        onAccepted: if (selectedIndex >= 0) app.soundMode = selectedIndex
    }

    SelectionDialog {
        id: vibrationModeDialog
        titleText: qsTr("Vibration")
        model: app.popupModeNames()
        onAccepted: if (selectedIndex >= 0) app.vibrationMode = selectedIndex
    }

    ListHeading {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        ListItemText { anchors.fill: heading.paddingItem; role: "Heading"; text: qsTr("Notifications") }
    }

    Flickable {
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        contentHeight: column.height + platformStyle.paddingLarge
        clip: true

        Column {
            id: column
            width: parent.width

            ListItem {
                id: popupsItem
                subItemIndicator: true
                Column {
                    anchors { left: popupsItem.paddingItem.left; right: popupsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Popup for new messages") }
                    Label {
                        width: parent.width
                        text: app.popupModeNames()[app.popupMode]
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { popupModeDialog.selectedIndex = app.popupMode; popupModeDialog.open() }
            }
            ListItem {
                id: soundItem
                subItemIndicator: true
                Column {
                    anchors { left: soundItem.paddingItem.left; right: soundItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Sound") }
                    Label {
                        width: parent.width
                        text: app.popupModeNames()[app.soundMode]
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { soundModeDialog.selectedIndex = app.soundMode; soundModeDialog.open() }
            }
            ListItem {
                id: vibrationItem
                subItemIndicator: true
                Column {
                    anchors { left: vibrationItem.paddingItem.left; right: vibrationItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Vibration") }
                    Label {
                        width: parent.width
                        text: app.popupModeNames()[app.vibrationMode]
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { vibrationModeDialog.selectedIndex = app.vibrationMode; vibrationModeDialog.open() }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("For messages that arrive while another application is in front. Each channel: Off, only the first new message, or every message. The sound rides the popup, so it plays only when a popup shows.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: groupsItem
                ListItemText {
                    anchors { left: groupsItem.paddingItem.left; right: groupsSwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Groups and channels")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: groupsSwitch
                    anchors { right: groupsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.groupNotifications
                    onCheckedChanged: if (checked != app.groupNotifications) app.groupNotifications = checked
                }
                onClicked: groupsSwitch.checked = !groupsSwitch.checked
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Also alert for messages in groups and channels. Chats muted in Telegram stay quiet. Symbigram stays connected in the background either way.")
            }
        }
    }
}
