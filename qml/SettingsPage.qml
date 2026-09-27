// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    QueryDialog {
        id: clearCacheDialog
        titleText: qsTr("Clear cache")
        message: qsTr("Delete the %1 of downloaded media held on the phone? It will be fetched again when needed.").arg(app.cacheSize)
        acceptButtonText: qsTr("Clear")
        rejectButtonText: qsTr("Cancel")
        onAccepted: app.clearCache()
    }

    SelectionDialog {
        id: languageDialog
        titleText: qsTr("App language")
        model: ListModel {
            ListElement { name: "System default"; code: "" }
            ListElement { name: "English"; code: "en" }
            ListElement { name: "Русский"; code: "ru" }
            ListElement { name: "Українська"; code: "uk" }
            ListElement { name: "Tiếng Việt"; code: "vi" }
            ListElement { name: "עברית"; code: "he" }
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

    SelectionDialog {
        id: downloadsDialog
        titleText: qsTr("Save downloads to")
        // the present drives, then a "Choose folder..." entry that opens the native picker
        model: app.downloadDrives.concat([qsTr("Choose folder...")])
        delegate: Item {
            width: parent ? parent.width : 300
            height: (typeof privateStyle != "undefined") ? privateStyle.menuItemHeight : 56
            Rectangle { anchors.fill: parent; color: dlMouse.pressed ? "#3d5a80" : "transparent" }
            Label {
                anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
                text: modelData + (index == app.downloadDriveIndex && !app.downloadCustom ? "   *" : "")
                color: index == app.downloadDrives.length ? "#8fd1ff" : "white"
                elide: Text.ElideRight
            }
            MouseArea { id: dlMouse; anchors.fill: parent; onClicked: { downloadsDialog.selectedIndex = index; downloadsDialog.accept() } }
        }
        onAccepted: {
            if (selectedIndex < 0) return
            if (selectedIndex == app.downloadDrives.length) app.chooseDownloadFolder()
            else app.downloadDriveIndex = selectedIndex
        }
    }

    SelectionDialog {
        id: imagePreviewDialog
        titleText: qsTr("Image preview")
        model: app.imagePreviewNames()
        onAccepted: if (selectedIndex >= 0) app.imagePreview = selectedIndex
    }

    CommonDialog {
        id: proxyDialog
        titleText: qsTr("SOCKS5 proxy")
        buttonTexts: [qsTr("Save"), qsTr("Cancel")]
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
        onButtonClicked: if (index == 0) app.saveProxy(true, proxyHostField.text, proxyPortField.text, proxyUserField.text, proxyPassField.text)
        function load() {
            proxyHostField.text = app.proxyHost
            proxyPortField.text = app.proxyPort
            proxyUserField.text = app.proxyUser
            proxyPassField.text = app.proxyPass
        }
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
                id: notificationsItem
                subItemIndicator: true
                Column {
                    anchors { left: notificationsItem.paddingItem.left; right: notificationsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Notifications") }
                    Label {
                        width: parent.width
                        text: (app.popupMode || app.soundMode || app.vibrationMode)
                              ? qsTr("Popup: %1, Sound: %2, Vibration: %3")
                                  .arg(app.popupModeNames()[app.popupMode])
                                  .arg(app.popupModeNames()[app.soundMode])
                                  .arg(app.popupModeNames()[app.vibrationMode])
                              : qsTr("Off")
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: pageStack.push(notificationsPage)
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Popup, sound and vibration for messages that arrive while another application is in front.")
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
            ListItem {
                id: proxyItem
                ListItemText {
                    anchors { left: proxyItem.paddingItem.left; right: proxySwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("SOCKS5 proxy")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: proxySwitch
                    anchors { right: proxyItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.proxyEnabled
                    onCheckedChanged: if (checked != app.proxyEnabled) app.setProxyEnabled(checked)
                }
                onClicked: proxySwitch.checked = !proxySwitch.checked
            }
            ListItem {
                id: proxyServerItem
                subItemIndicator: true
                enabled: app.proxyEnabled
                opacity: enabled ? 1 : 0.4
                Column {
                    anchors { left: proxyServerItem.paddingItem.left; right: proxyServerItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Proxy server") }
                    Label {
                        width: parent.width
                        text: app.proxyHost != "" ? (app.proxyHost + ":" + app.proxyPort) : qsTr("not set")
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { proxyDialog.load(); proxyDialog.open() }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Route the connection through a SOCKS5 proxy. Changing it reconnects.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: downloadsItem
                subItemIndicator: true
                Column {
                    anchors { left: downloadsItem.paddingItem.left; right: downloadsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Save downloads to") }
                    Label {
                        width: parent.width
                        text: app.downloadFolder
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideMiddle
                    }
                }
                onClicked: { downloadsDialog.selectedIndex = app.downloadDriveIndex; downloadsDialog.open() }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Saved photos and files go here. Pick a drive, or \"Choose folder...\" for any folder.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: imagePreviewItem
                subItemIndicator: true
                Column {
                    anchors { left: imagePreviewItem.paddingItem.left; right: imagePreviewItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Image preview") }
                    Label {
                        width: parent.width
                        text: app.imagePreviewNames()[app.imagePreview]
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { imagePreviewDialog.selectedIndex = app.imagePreview; imagePreviewDialog.open() }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Thumbnail shows a small blurred preview until you tap a photo to load it (saves data). Full loads photo previews automatically. Neither changes what Save downloads.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: cacheItem
                Column {
                    anchors { left: cacheItem.paddingItem.left; right: cacheItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Clear cache") }
                    Label {
                        width: parent.width
                        text: qsTr("Downloaded media held on the phone: %1").arg(app.cacheSize)
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideMiddle
                    }
                }
                onClicked: clearCacheDialog.open()
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("The app's private store of downloaded photos, avatars and files (separate from the folder above). Clearing it just re-downloads on demand.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: logItem
                ListItemText {
                    anchors { left: logItem.paddingItem.left; right: logSwitch.left; verticalCenter: parent.verticalCenter }
                    role: "Title"
                    text: qsTr("Keep a log")
                    wrapMode: Text.Wrap
                }
                Switch {
                    id: logSwitch
                    anchors { right: logItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    checked: app.logging
                    onCheckedChanged: if (checked != app.logging) app.logging = checked
                }
                onClicked: logSwitch.checked = !logSwitch.checked
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Off by default. Turn it on only to collect a diagnostic log (shown on the About page) when something goes wrong.")
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
