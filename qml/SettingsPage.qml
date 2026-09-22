// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    SelectionDialog {
        id: languageDialog
        titleText: qsTr("App language")
        model: ListModel {
            ListElement { name: "System default"; code: "" }
            ListElement { name: "English"; code: "en" }
            ListElement { name: "Русский"; code: "ru" }
            ListElement { name: "Українська"; code: "uk" }
        }
        // The stock delegate shows modelData (a string list); this model has roles, and the
        // phone theme's dialog text is hard to read - so: our own rows, white on the dialog.
        delegate: Item {
            width: parent ? parent.width : 300
            height: (typeof privateStyle != "undefined") ? privateStyle.menuItemHeight : 56
            Rectangle { anchors.fill: parent; color: rowMouse.pressed ? "#3d5a80" : "transparent" }
            Label {
                anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; verticalCenter: parent.verticalCenter }
                text: (index == 0 ? qsTr("System default") : model.name) + (model.code == app.language ? "   *" : "")
                color: "white"
                elide: Text.ElideRight
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                onClicked: { languageDialog.selectedIndex = index; languageDialog.accept() }
            }
        }
        onAccepted: if (selectedIndex >= 0) app.language = model.get(selectedIndex).code
    }

    function languageName() {
        for (var i = 0; i < languageDialog.model.count; ++i)
            if (languageDialog.model.get(i).code == app.language)
                return i == 0 ? qsTr("System default") : languageDialog.model.get(i).name
        return qsTr("System default")
    }

    ListHeading {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        ListItemText { anchors.fill: heading.paddingItem; role: "Heading"; text: qsTr("Settings") }
    }

    Flickable {
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        contentHeight: column.height + platformStyle.paddingLarge
        clip: true

        Column {
            id: column
            width: parent.width

            ListItem {
                id: notifyItem
                ListItemText {
                    anchors { left: notifyItem.paddingItem.left; right: notifySwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Notifications")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: notifySwitch
                    anchors { right: notifyItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.notifications
                    onCheckedChanged: if (checked != app.notifications) app.notifications = checked
                }
                onClicked: notifySwitch.checked = !notifySwitch.checked
            }
            ListItem {
                id: popupsItem
                enabled: app.notifications
                opacity: enabled ? 1 : 0.4
                ListItemText {
                    anchors { left: popupsItem.paddingItem.left; right: popupsSwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Popup for new messages")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: popupsSwitch
                    anchors { right: popupsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.popups
                    onCheckedChanged: if (checked != app.popups) app.popups = checked
                }
                onClicked: popupsSwitch.checked = !popupsSwitch.checked
            }
            ListItem {
                id: vibrateItem
                enabled: app.notifications
                opacity: enabled ? 1 : 0.4
                ListItemText {
                    anchors { left: vibrateItem.paddingItem.left; right: vibrateSwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Vibrate")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: vibrateSwitch
                    anchors { right: vibrateItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.vibrate
                    onCheckedChanged: if (checked != app.vibrate) app.vibrate = checked
                }
                onClicked: vibrateSwitch.checked = !vibrateSwitch.checked
            }
            ListItem {
                id: groupsItem
                enabled: app.notifications
                opacity: enabled ? 1 : 0.4
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
                text: qsTr("For messages that arrive while another application is in front: the \"new messages\" dialog, the popup and the vibration. Chats muted in Telegram stay quiet. Symbigram stays connected in the background either way.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: autoItem
                ListItemText {
                    anchors { left: autoItem.paddingItem.left; right: autoSwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Connect on start")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: autoSwitch
                    anchors { right: autoItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.autoConnect
                    onCheckedChanged: if (checked != app.autoConnect) app.autoConnect = checked
                }
                onClicked: autoSwitch.checked = !autoSwitch.checked
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: languageItem
                subItemIndicator: true
                Column {
                    anchors { left: languageItem.paddingItem.left; right: languageItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("App language") }
                    Label {
                        width: parent.width
                        text: languageName()
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: {
                    for (var i = 0; i < languageDialog.model.count; ++i)
                        if (languageDialog.model.get(i).code == app.language) languageDialog.selectedIndex = i
                    languageDialog.open()
                }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Takes effect after the app is restarted.")
            }
        }
    }
}
