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

    ListHeading {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        ListItemText { anchors.fill: heading.paddingItem; role: "Heading"; text: app.topics.title }
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
                    anchors { left: icon.right; leftMargin: platformStyle.paddingMedium; right: badge.left; rightMargin: platformStyle.paddingSmall; verticalCenter: parent.verticalCenter }
                    ListItemText {
                        width: parent.width
                        role: "Title"
                        text: (model.closed ? qsTr("[closed] ") : "") + model.title
                    }
                }
                Rectangle {
                    id: badge
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    visible: model.unread > 0
                    height: 24
                    width: Math.max(24, unreadLabel.width + 12)
                    radius: 12
                    color: "#5b8fd0"
                    Label {
                        id: unreadLabel
                        anchors.centerIn: parent
                        text: model.unread > 99 ? "99+" : model.unread
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                    }
                }
            }
            onClicked: window.openTopic(model.topicId, model.title)
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
