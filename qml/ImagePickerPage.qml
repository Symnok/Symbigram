// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// "Attach > Image": a thumbnail grid of the phone's pictures, newest first.
//
// This is Symbigram's own grid rather than the platform's Photos picker, which froze or came up
// blank on several phones (see imagepicker.h). Thumbnails decode lazily for visible cells only,
// via sourceSize, so scrolling a full card stays cheap.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    /// Emitted with a native path once something is tapped; the caller stages it for sending.
    signal picked(string path)

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
        ToolButton { iconSource: "toolbar-refresh"; onClicked: app.imagePicker.rescan() }
    }

    // Rescan on every visit: a photo taken since last time then shows up, and there is no cache
    // to go stale. The scan is incremental, so this cannot block the UI.
    Component.onCompleted: app.imagePicker.rescan()

    // Same header shape and height as a chat, so the pages line up.
    Rectangle {
        id: heading
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: platformStyle.graphicSizeMedium + 2 * platformStyle.paddingMedium
        color: "#1c2a3a"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: "#3d5a80" }
        Column {
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            Label { width: parent.width; text: qsTr("Choose an image"); font.bold: true; color: "white"; elide: Text.ElideRight }
            Label {
                width: parent.width
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
                elide: Text.ElideRight
                text: app.imagePicker.scanning
                      ? qsTr("looking for pictures... %1").arg(app.imagePicker.found)
                      : qsTr("%1 images").arg(app.imagePicker.count)
            }
        }
    }

    GridView {
        id: grid
        anchors { top: heading.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        // Three across, whatever the screen width, with the cells square.
        cellWidth: Math.floor(width / 3)
        cellHeight: cellWidth
        cacheBuffer: cellHeight * 2
        model: app.imagePicker

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors { fill: parent; margins: 1 }
                color: "#10181f"

                // The cached thumbnail, not the picture itself: a full JPEG per cell is what made
                // this grid slow. Empty until the thumbnailer has one, hence the busy indicator.
                Image {
                    id: thumb
                    anchors.fill: parent
                    source: model.thumb
                    sourceSize.width: grid.cellWidth
                    sourceSize.height: grid.cellHeight
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    clip: true
                }

                // Delegates exist only for cells in or near view, so this asks for exactly the
                // thumbnails that are needed, in the order they are revealed.
                Component.onCompleted: if (model.thumb == "") app.imagePicker.requestThumb(index, grid.cellWidth)

                BusyIndicator {
                    anchors.centerIn: parent
                    running: model.thumb == "" || thumb.status == Image.Loading
                    visible: running
                    width: platformStyle.graphicSizeSmall
                    height: platformStyle.graphicSizeSmall
                }

                // A picture the phone cannot decode should not look like an empty cell.
                Label {
                    anchors.centerIn: parent
                    visible: thumb.status == Image.Error
                    text: "?"
                    color: platformStyle.colorNormalMid
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        page.picked(app.imagePicker.pathAt(index))
                        pageStack.pop()
                    }
                }
            }
        }
    }

    ScrollDecorator { flickableItem: grid }

    // Pictures kept somewhere unusual are still reachable - the folder scan is a convenience,
    // not the only way in.
    Column {
        anchors.centerIn: grid
        width: parent.width - 4 * platformStyle.paddingLarge
        spacing: platformStyle.paddingLarge
        visible: !app.imagePicker.scanning && app.imagePicker.count == 0
        Label {
            width: parent.width
            text: qsTr("No pictures found in the phone's image folders.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: platformStyle.colorNormalMid
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Browse files...")
            onClicked: {
                var p = app.pickAttachment(true)
                if (p != "") { page.picked(p); pageStack.pop() }
            }
        }
    }
}
