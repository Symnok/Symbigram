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
        // One literal, not a concatenation: lupdate cannot extract a joined string.
        message: qsTr("Delete the %1 held on the phone? This clears the media cache and the files downloaded into the Symbigram folders. Installed language packs are kept.").arg(app.cacheSize)
        acceptButtonText: qsTr("Clear")
        rejectButtonText: qsTr("Cancel")
        onAccepted: app.clearCache()
    }

    SelectionDialog {
        id: languageDialog
        titleText: qsTr("App language")
        // Built-in languages plus any add-on pack found in the translations folder.
        model: app.availableLanguages()
        // The stock delegate shows modelData (a string list); this model has roles, and the
        // phone theme's dialog text is hard to read - so: our own rows, white on the dialog.
        delegate: Item {
            width: parent ? parent.width : 300
            height: (typeof privateStyle != "undefined") ? privateStyle.menuItemHeight : 56
            Rectangle { anchors.fill: parent; color: rowMouse.pressed ? "#3d5a80" : "transparent" }
            Label {
                anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; verticalCenter: parent.verticalCenter }
                text: (index == 0 ? qsTr("System default") : modelData.name) + (modelData.code == app.language ? "   *" : "")
                color: "white"
                elide: Text.ElideRight
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                onClicked: { languageDialog.selectedIndex = index; languageDialog.accept() }
            }
        }
        onAccepted: if (selectedIndex >= 0) app.language = model[selectedIndex].code
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

    SelectionDialog {
        id: fontSizeDialog
        titleText: qsTr("Font size")
        model: app.fontSizeNames()
        onAccepted: if (selectedIndex >= 0) app.fontSize = selectedIndex
    }

    // A page, not a dialog: a text field inside a CommonDialog cannot be pasted into on this
    // platform. Shared with the login screen, which needs the same MTProto fields.
    Component { id: proxyPage; ProxyPage {} }

    function languageName() {
        var list = app.availableLanguages()
        for (var i = 0; i < list.length; ++i)
            if (list[i].code == app.language)
                return i == 0 ? qsTr("System default") : list[i].name
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
                    text: qsTr("Proxy")
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
                        text: app.proxyHost == "" ? qsTr("not set")
                              : (app.proxyHost + ":" + app.proxyPort
                                 + (app.proxyKind == 1 ? qsTr(" (MTProto)") : qsTr(" (SOCKS5)")))
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: pageStack.push(proxyPage)
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Route the connection through a SOCKS5 or MTProto proxy. Changing it reconnects.")
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
            ListItem {
                id: fontSizeItem
                subItemIndicator: true
                Column {
                    anchors { left: fontSizeItem.paddingItem.left; right: fontSizeItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Font size") }
                    Label {
                        width: parent.width
                        text: app.fontSizeNames()[app.fontSize]
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                onClicked: { fontSizeDialog.selectedIndex = app.fontSize; fontSizeDialog.open() }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("The size of the message text in chats. Small is the original size.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Thumbnail shows a small blurred preview until you tap a photo to load it (saves data). Full loads photo previews automatically. None shows no picture at all until you tap. Video previews are never loaded automatically. None of this changes what Save downloads.")
            }
            Item { width: 1; height: platformStyle.paddingLarge }

            ListItem {
                id: cacheItem
                Column {
                    anchors { left: cacheItem.paddingItem.left; right: cacheItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Clear cache") }
                    Label {
                        width: parent.width
                        text: qsTr("Cached media and downloaded files: %1").arg(app.cacheSize)
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
                text: qsTr("The app's private store of downloaded photos, avatars and files, plus its own scratch copies. Clearing it just re-downloads on demand. Files you saved or opened yourself are left alone.")
            }

            // Read-only, but the path is spelled out: the thumbnails are not part of Clear cache,
            // so anyone who needs the space back has to be able to find the folder and empty it
            // themselves. Where it is follows "Save downloads to" above.
            ListItem {
                id: thumbsItem
                enabled: false
                Column {
                    anchors { left: thumbsItem.paddingItem.left; right: thumbsItem.paddingItem.right; verticalCenter: parent.verticalCenter }
                    ListItemText { width: parent.width; role: "Title"; text: qsTr("Image thumbnails") }
                    ListItemText {
                        width: parent.width
                        role: "SubTitle"
                        text: app.thumbnailCacheInfo
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
            Label {
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.Wrap
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                text: qsTr("Small copies of your photos, so the gallery grid opens quickly. They sit in a thumbnails folder inside the cache folder above, and move with it if you change it. Clear cache leaves them alone - delete the folder by hand when you need the space.")
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

            // Where to find it after the fact - the whole point of writing it to disk is that it
            // survives a crash or a freeze, when the in-app view is gone with the app.
            Label {
                visible: app.logging
                width: parent.width - 2 * platformStyle.paddingLarge
                x: platformStyle.paddingLarge
                wrapMode: Text.WrapAnywhere
                color: platformStyle.colorNormalMid
                font.pixelSize: platformStyle.fontSizeSmall
                text: qsTr("Also written to %1, so it survives a freeze or a restart.").arg(app.logLocation())
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
                    var list = app.availableLanguages()
                    for (var i = 0; i < list.length; ++i)
                        if (list[i].code == app.language) languageDialog.selectedIndex = i
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
