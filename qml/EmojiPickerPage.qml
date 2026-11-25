// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// "Attach > Emoji": a grid of the emoji Symbigram has artwork for.
//
// Only emoji WITH artwork are offered - one that cannot be drawn would be a blank cell here and
// an invisible result in the message. Tapping one emits its SHORTCODE, not the emoji itself: the
// composer is a plain text field drawn in the phone font, which has no emoji glyph, so a real
// character would appear there as an empty box that says nothing about what was picked. The
// shortcode becomes a real emoji when the message is sent.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    /// Emitted with a shortcode such as ":thumbs_up:" once something is tapped.
    signal picked(string code)

    // Built once. emojiPalette() assembles a list of maps, which is not something to re-run on
    // every delegate binding.
    property variant palette: app.emojiPalette()

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    // Same header shape and height as a chat, so the pages line up.
    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Column {
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: qsTr("Choose an emoji"); font.bold: true; color: "white"; elide: Text.ElideRight }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                elide: Text.ElideRight
                text: qsTr("%1 available - sent as a real emoji").arg(page.palette.length)
            }
        }
    }

    GridView {
        id: grid
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: hint.top }
        clip: true
        // Five across, whatever the screen width, with the cells square - a tap target
        // comfortably larger than the 24px artwork it holds.
        cellWidth: Math.floor(width / 5)
        cellHeight: cellWidth
        cacheBuffer: cellHeight * 3
        model: page.palette

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                color: "#40ffffff"
                visible: tap.pressed
                radius: 4
            }
            Image {
                anchors.centerIn: parent
                source: modelData.url
                // The artwork is 24px; drawn a little larger because this is a tap target rather
                // than running text. sourceSize keeps the decode at the size actually drawn.
                width: 32
                height: 32
                sourceSize.width: 32
                sourceSize.height: 32
                smooth: true
                asynchronous: true
            }
            MouseArea {
                id: tap
                anchors.fill: parent
                onClicked: {
                    page.picked(modelData.code)
                    pageStack.pop()
                }
            }
        }

        ScrollDecorator { flickableItem: grid }
    }

    Label {
        id: hint
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: platformStyle.paddingMedium }
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: platformStyle.fontSizeSmall
        color: platformStyle.colorNormalMid
        text: qsTr("While you type it shows as a short name such as :thumbs_up:, because this phone's font cannot draw an emoji. It is sent as a real one.")
    }
}
