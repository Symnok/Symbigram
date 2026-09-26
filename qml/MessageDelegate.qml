// Symbigram - a Telegram client for Symbian Anna/Belle.
// Copyright (C) 2026 - GPL-3.0-or-later, see LICENSE.
//
// One message: date separator, sender (in groups), forwarded/reply lines, the attachment
// (a photo shown inline, or a file row with a download/save control) and/or text, time and
// delivery marks. Outgoing on the right, incoming on the left, service messages centred.
import QtQuick 1.1
import com.nokia.symbian 1.1

Item {
    id: root
    signal pressAndHold
    // tapping a ready image opens the viewer; a file row's button downloads / saves / opens
    signal openImage(string path, int row)

    property int maxBubbleWidth: width * 0.82
    // "29s" under a minute, "M:SS" above; blank when no self-destruct timer.
    // The first URL in the text (for tapping), with a scheme added for www. links.
    function firstLink(t) {
        var m = t.match(/(https?:\/\/|www\.)[^\s]+/)
        if (!m) return ""
        var u = m[0]
        return u.indexOf("http") === 0 ? u : "http://" + u
    }

    // Escape HTML, turn URLs (http(s):// or www.) into tappable links, keep line breaks.
    function linkify(t) {
        var s = t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        s = s.replace(/((https?:\/\/|www\.)[^\s<]+)/g, function(m) {
            var href = m.indexOf("http") === 0 ? m : "http://" + m
            return '<a href="' + href + '"><font color="#8fd1ff">' + m + '</font></a>'
        })
        return s.replace(/\n/g, "<br/>")
    }

    function burnText(s) {
        if (s < 0) return ""
        if (s < 60) return s + "s"
        var m = Math.floor(s / 60), ss = s % 60
        return m + ":" + (ss < 10 ? "0" + ss : ss)
    }
    property bool isPhoto: model.mediaKind == "photo" || model.mediaKind == "sticker" || model.mediaKind == "gif"
    property bool hasFileRow: model.mediaKind != "" && !isPhoto

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
            function w(item) { return item.visible ? item.paintedWidth : 0 }
            property real textWidth: Math.max(w(bodyLabel), Math.max(w(senderLabel), Math.max(w(fwdLabel), w(replyLabel))))
            property real innerWidth: Math.max(Math.max(textWidth, timeRow.width),
                                               isPhoto && photo.shownWidth > 0 ? photo.shownWidth : (hasFileRow ? fileRow.width : 0))
            width: Math.min(maxBubbleWidth, innerWidth + 2 * platformStyle.paddingMedium)
            height: inner.height + timeRow.height + 2 * platformStyle.paddingMedium + platformStyle.paddingSmall
            radius: 8
            color: model.failed ? "#6b2b2b" : (model.out ? "#1f5e8a" : "#3a3a3a")
            opacity: model.pending ? 0.6 : 1
            anchors { right: model.out ? parent.right : undefined; left: model.out ? undefined : parent.left; margins: platformStyle.paddingMedium }

            MouseArea {
                anchors.fill: parent
                onPressAndHold: root.pressAndHold()
                onClicked: {
                    if (isPhoto && model.mediaState == "ready") root.openImage(model.localPath != "" ? model.localPath : model.mediaThumb, index)
                    else if (isPhoto && model.mediaState == "idle") chat.downloadMedia(index)
                }
            }

            Column {
                id: inner
                anchors { left: parent.left; top: parent.top; margins: platformStyle.paddingMedium }
                width: maxBubbleWidth - 2 * platformStyle.paddingMedium
                spacing: 3

                Label {
                    id: senderLabel
                    visible: model.sender != ""
                    text: model.sender
                    font.bold: true
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: "#8fd1ff"
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
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
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
                Label {
                    id: replyLabel
                    visible: model.reply != ""
                    text: model.reply
                    font.pixelSize: platformStyle.fontSizeSmall
                    color: "#b8d8f0"
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }

                // -- a photo / sticker shown inline --
                Item {
                    id: photo
                    visible: isPhoto
                    property real maxW: maxBubbleWidth - 2 * platformStyle.paddingMedium
                    property real ar: (model.mediaWidth > 0 && model.mediaHeight > 0) ? (model.mediaHeight / model.mediaWidth) : 0.75
                    property real shownWidth: image.status == Image.Ready ? Math.min(maxW, image.sourceSize.width, 260) : Math.min(maxW, 200)
                    width: shownWidth
                    height: shownWidth * (image.status == Image.Ready ? (image.sourceSize.height / image.sourceSize.width) : ar)
                    Rectangle { anchors.fill: parent; radius: 6; color: "#20000000"; visible: image.status != Image.Ready }
                    Image {
                        id: image
                        anchors.fill: parent
                        source: model.mediaThumb
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                        smooth: true
                        asynchronous: true
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: model.mediaState == "loading" && image.status != Image.Ready
                        visible: running
                    }
                    // a "tap to load" hint when nothing is showing yet
                    Label {
                        anchors.centerIn: parent
                        visible: model.mediaState == "idle" && image.status != Image.Ready
                        text: qsTr("Tap to load")
                        color: "white"
                        font.pixelSize: platformStyle.fontSizeSmall
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: model.mediaState == "failed"
                        text: qsTr("Failed - tap to retry")
                        color: "#ff9b9b"
                        font.pixelSize: platformStyle.fontSizeSmall
                    }
                }

                // -- a file / video / voice row --
                Row {
                    id: fileRow
                    visible: hasFileRow
                    spacing: platformStyle.paddingMedium
                    Rectangle {
                        width: platformStyle.graphicSizeMedium
                        height: platformStyle.graphicSizeMedium
                        radius: 6
                        color: "#5a86b0"
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            anchors.centerIn: parent
                            color: "white"
                            font.pixelSize: platformStyle.fontSizeSmall
                            text: model.mediaKind == "voice"
                                ? (model.voicePlaying ? "■" : "▶")
                                : (model.mediaKind == "audio"
                                   ? (chat.audioRow == index ? chat.audioBuffer + "%" : "▶")
                                   : (model.mediaState == "ready" ? "✓"
                                   : (model.mediaState == "loading" ? model.mediaProgress + "%"
                                   : (model.mediaKind == "video" ? "▶" : "↓"))))
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (model.mediaKind == "voice") chat.playVoice(index)
                                else if (model.mediaKind == "audio") chat.streamAudio(index)
                                else if (model.mediaState == "ready") chat.openMedia(index)
                                else if (model.mediaState != "loading") chat.downloadMedia(index)
                            }
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            text: model.mediaKind == "voice" ? qsTr("Voice message")
                                : (model.mediaKind == "video" ? qsTr("Video") : model.mediaInfo)
                            color: "white"
                            font.pixelSize: platformStyle.fontSizeSmall
                            width: maxBubbleWidth - 2 * platformStyle.paddingMedium - platformStyle.graphicSizeMedium - platformStyle.paddingMedium
                            elide: Text.ElideMiddle
                        }
                        Label {
                            text: model.mediaKind == "voice"
                                ? (model.voicePlaying ? qsTr("playing... tap to stop")
                                   : (model.mediaState == "loading" ? qsTr("loading %1%").arg(model.mediaProgress) : qsTr("tap to play")))
                                : (model.mediaKind == "audio"
                                   ? (chat.audioRow == index ? qsTr("buffering %1%... opening player").arg(chat.audioBuffer) : qsTr("tap to play in player"))
                                   : (model.mediaState == "ready" ? qsTr("tap to open") : (model.mediaState == "loading" ? qsTr("downloading %1%").arg(model.mediaProgress) : (model.mediaKind == "video" ? model.mediaInfo : qsTr("tap to download")))))
                            color: "#c0d4e6"
                            font.pixelSize: platformStyle.fontSizeSmall * 0.85
                        }
                    }
                }

                Label {
                    id: bodyLabel
                    width: parent.width
                    text: root.linkify(model.body)
                    visible: model.body != ""
                    wrapMode: Text.Wrap
                    // The label spans the max bubble width while the bubble shrinks to the painted
                    // text; Qt would auto-align RTL (Persian/Arabic/Hebrew) text to the far right of
                    // that full width - outside the shrunken bubble, so it looks empty. Pin it left
                    // so short RTL text stays inside the bubble (LTR is unchanged).
                    horizontalAlignment: Text.AlignLeft
                    color: "white"
                    textFormat: Text.StyledText
                    onLinkActivated: Qt.openUrlExternally(link)
                    // Qt Quick 1.1 Text doesn't reliably grab link taps under the bubble's mouse
                    // area, so a tap on body text with a URL opens it; long-press still menus.
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.firstLink(model.body) != ""
                        onClicked: { var u = root.firstLink(model.body); if (u != "") Qt.openUrlExternally(u) }
                        onPressAndHold: root.pressAndHold()
                    }
                }
                Label {
                    id: noteLabel
                    width: parent.width
                    visible: model.body == "" && !isPhoto && !hasFileRow && model.note != ""
                    text: "[" + model.note + "]"
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    font.italic: true
                    color: "#d0d0d0"
                }
            }

            Row {
                id: timeRow
                anchors { right: parent.right; bottom: parent.bottom; margins: platformStyle.paddingSmall }
                spacing: platformStyle.paddingSmall
                Rectangle {
                    // a small "burning" dot next to the self-destruct countdown
                    visible: model.secretBurn === true && model.secretRemaining >= 0
                    width: 8; height: 8; radius: 4
                    color: "#ffb14e"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    visible: model.secretBurn === true && model.secretRemaining >= 0
                    text: root.burnText(model.secretRemaining)
                    font.pixelSize: platformStyle.fontSizeSmall * 0.85
                    font.bold: true
                    color: "#ffb14e"
                }
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
