// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// The topic list of a forum supergroup. Tapping a topic opens its messages (a normal ChatPage
// in topic mode). Reached from the chat list when the group is a forum.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
        ToolButton { iconSource: "toolbar-menu"; onClicked: topicsMenu.open() }
    }

    Menu {
        id: topicsMenu
        MenuLayout {
            MenuItem { text: qsTr("Reload topics"); onClicked: app.topics.refresh() }
        }
    }

    ContextMenu {
        id: topicContextMenu
        property int topicId: 0
        property bool topicMuted: false
        MenuLayout {
            MenuItem { text: qsTr("Mark all as read"); onClicked: app.topics.markRead(topicContextMenu.topicId) }
            MenuItem {
                text: topicContextMenu.topicMuted ? qsTr("Unmute topic") : qsTr("Mute topic")
                onClicked: app.topics.setMuted(topicContextMenu.topicId, !topicContextMenu.topicMuted)
            }
        }
    }

    // Same shape and height as a chat's header, so switching between them does not jump.
    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Rectangle {
            id: headingAvatar
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            width: platformStyle.graphicSizeMedium
            height: platformStyle.graphicSizeMedium
            radius: width / 2
            color: "#3d5a80"
            Label {
                anchors.centerIn: parent
                text: app.topics.title.length > 0 ? app.topics.title.charAt(0).toUpperCase() : "#"
                color: "white"; font.bold: true
            }
        }
        Column {
            anchors { left: headingAvatar.right; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label {
                width: parent.width
                text: app.topics.title
                elide: Text.ElideRight
                font.bold: true
                color: "white"
            }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                text: app.topics.loading ? qsTr("loading topics...") : qsTr("%1 topics").arg(app.topics.count)
                elide: Text.ElideRight
                color: platformStyle.colorNormalMid
            }
        }
    }

    ListView {
        id: list
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        model: app.topics
        clip: true
        focus: true
        highlight: Rectangle { color: "#3d5a80"; opacity: 0.35 }
        highlightMoveDuration: 120

        delegate: ListItem {
            id: item
            subItemIndicator: true
            Item {
                anchors { fill: item.paddingItem }
                Rectangle {
                    id: icon
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: platformStyle.graphicSizeSmall
                    height: platformStyle.graphicSizeSmall
                    radius: 6
                    color: model.color
                    Label {
                        anchors.centerIn: parent
                        text: model.title.length > 0 ? model.title.charAt(0).toUpperCase() : "#"
                        color: "white"; font.bold: true
                    }
                }
                Column {
                    anchors { left: icon.right; leftMargin: platformStyle.paddingMedium; right: rightRow.left; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
                    ListItemText {
                        width: parent.width
                        role: "Title"
                        text: (model.closed ? qsTr("[closed] ") : "") + model.title
                    }
                }
                Row {
                    id: rightRow
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: platformStyle.paddingSmall
                    // Positioners lay out only visible children, so the icon/badge appear as needed.
                    Image {
                        source: "qrc:/images/muted.png"
                        visible: model.muted === true
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Rectangle {
                        id: badge
                        anchors.verticalCenter: parent.verticalCenter
                        visible: model.unread > 0
                        height: 24
                        width: Math.max(24, unreadLabel.width + 12)
                        radius: 12
                        color: model.muted === true ? "#5a6674" : "#5b8fd0"
                        Label {
                            id: unreadLabel
                            anchors.centerIn: parent
                            text: model.unread > 99 ? "99+" : model.unread
                            color: "white"
                            font.pixelSize: platformStyle.fontSizeSmall
                        }
                    }
                }
            }
            onClicked: window.openTopic(model.topicId, model.title)
            onPressAndHold: { topicContextMenu.topicId = model.topicId; topicContextMenu.topicMuted = (model.muted === true); topicContextMenu.open() }
        }

        ScrollDecorator { flickableItem: list }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: app.topics.loading
        visible: running
        width: platformStyle.graphicSizeLarge
        height: platformStyle.graphicSizeLarge
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * platformStyle.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: platformStyle.colorNormalMid
        visible: !app.topics.loading && app.topics.count == 0
        text: app.topics.error != "" ? app.topics.error : qsTr("No topics.")
    }
}
