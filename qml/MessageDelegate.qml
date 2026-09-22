// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One message: date separator, sender (in groups), forwarded/reply lines, text or the
// attachment note, time and delivery marks. Outgoing on the right, incoming on the left,
// service messages centred.
import QtQuick 1.1
import com.nokia.symbian 1.1

Item {
    id: root
    signal pressAndHold

    property int maxBubbleWidth: width * 0.82

    height: column.height + platformStyle.paddingSmall

    Column {
        id: column
        anchors { left: parent.left; right: parent.right }
        spacing: platformStyle.paddingSmall

        Item {
            width: parent.width
            height: model.showDate ? dateLabel.height + platformStyle.paddingMedium : 0
            visible: model.showDate
            Label {
                id: dateLabel
                anchors.centerIn: parent
                text: model.dateText
                font.pixelSize: platformStyle.fontSizeSmall
                color: platformStyle.colorNormalMid
            }
        }

        // service messages: one centred line
        Label {
            width: parent.width - 4 * platformStyle.paddingLarge
            anchors.horizontalCenter: parent.horizontalCenter
            visible: model.service
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: platformStyle.fontSizeSmall
            color: platformStyle.colorNormalMid
            text: (model.sender != "" ? model.sender + " " : "") + model.note
        }

        Rectangle {
            id: bubble
            visible: !model.service
            function w(label) { return label.visible ? label.paintedWidth : 0 }
            property real innerWidth: Math.max(Math.max(w(bodyLabel), Math.max(w(noteLabel), timeRow.width)),
                                               Math.max(w(senderLabel), Math.max(w(fwdLabel), w(replyLabel))))
            width: Math.min(maxBubbleWidth, innerWidth + 2 * platformStyle.paddingMedium)
            height: visible ? inner.height + timeRow.height + 2 * platformStyle.paddingMedium + platformStyle.paddingSmall : 0
            radius: 8
            color: model.failed ? "#6b2b2b" : (model.out ? "#1f5e8a" : "#3a3a3a")
            opacity: model.pending ? 0.6 : 1
            anchors { right: model.out ? parent.right : undefined; left: model.out ? undefined : parent.left; margins: platformStyle.paddingMedium }

            MouseArea {
                anchors.fill: parent
                onPressAndHold: root.pressAndHold()
            }

            Column {
                id: inner
                anchors { left: parent.left; top: parent.top; margins: platformStyle.paddingMedium }
                width: maxBubbleWidth - 2 * platformStyle.paddingMedium
                spacing: 2
                Label {
                    id: senderLabel
                    visible: model.sender != ""
                    text: model.sender
                    font.bold: true
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: "#8fd1ff"
                    width: parent.width
                    elide: Text.ElideRight
                }
                Label {
                    id: fwdLabel
                    visible: model.forwarded != ""
                    text: qsTr("Forwarded from %1").arg(model.forwarded)
                    font.italic: true
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: "#b8d8f0"
                    width: parent.width
                    elide: Text.ElideRight
                }
                Label {
                    id: replyLabel
                    visible: model.reply != ""
                    text: model.reply
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: "#b8d8f0"
                    width: parent.width
                    elide: Text.ElideRight
                }
                Label {
                    id: bodyLabel
                    width: parent.width
                    text: model.body != "" ? model.body : ""
                    visible: model.body != ""
                    wrapMode: Text.Wrap
                    color: "white"
                }
                Label {
                    id: noteLabel
                    width: parent.width
                    visible: model.note != ""
                    text: "[" + model.note + "]"
                    wrapMode: Text.Wrap
                    font.italic: true
                    color: "#d0d0d0"
                }
            }

            Row {
                id: timeRow
                anchors { right: parent.right; bottom: parent.bottom; margins: platformStyle.paddingSmall }
                spacing: platformStyle.paddingSmall
                Label {
                    text: (model.edited ? qsTr("edited") + ", " : "") + model.timeText
                    font.pixelSize: platformStyle.fontSizeSmall * 0.85
                    color: "#c0c0c0"
                }
                Label {
                    visible: model.out
                    text: model.failed ? "!" : (model.pending ? "..." : (model.read ? "✓✓" : "✓"))
                    font.pixelSize: platformStyle.fontSizeSmall * 0.85
                    color: model.failed ? "#ff9b9b" : (model.read ? "#8fd1ff" : "#c0c0c0")
                }
            }
        }
    }
}
