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
    // Settings > Font size. 1.0 is "Small", the original message text size.
    property real fontScale: app.fontScale
    // "29s" under a minute, "M:SS" above; blank when no self-destruct timer.
    // The first URL in the text (for tapping), with a scheme added for www. links.
    // Every tappable target in the text, as a URL: real links, plus @mentions turned into
    // t.me links so they go through the same "open it inside Symbigram" handling.
    // The @ must follow a space or an opening bracket, which keeps e-mail addresses out.
    function linkTargets(t) {
        var out = []
        var re = /(https?:\/\/|www\.)[^\s]+|(?:^|[\s(\[])@([A-Za-z][A-Za-z0-9_]{4,31})/g
        var m
        while ((m = re.exec(t)) !== null) {
            if (m[2] !== undefined) {
                out.push("https://t.me/" + m[2])
            } else {
                var u = m[0]
                out.push(u.indexOf("http") === 0 ? u : "http://" + u)
            }
        }
        return out
    }

    function firstLink(t) {
        var a = linkTargets(t)
        return a.length > 0 ? a[0] : ""
    }

    // How many tappable targets the text holds. With more than one we must let Qt work out which
    // link was tapped (see bodyLabel) instead of always opening the first.
    function linkCount(t) {
        return linkTargets(t).length
    }

    // Escape HTML, turn URLs (http(s):// or www.) into tappable links, keep line breaks.
    function linkify(t) {
        var s = t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        s = s.replace(/((https?:\/\/|www\.)[^\s<]+)/g, function(m) {
            var href = m.indexOf("http") === 0 ? m : "http://" + m
            return '<a href="' + href + '"><font color="#8fd1ff">' + m + '</font></a>'
        })
        // @mentions become t.me links (the lead character is kept so e-mails are not touched).
        s = s.replace(/(^|[\s(\[])@([A-Za-z][A-Za-z0-9_]{4,31})/g, function(m, lead, name) {
            return lead + '<a href="https://t.me/' + name + '"><font color="#8fd1ff">@' + name + '</font></a>'
        })
        return s.replace(/\n/g, "<br/>")
    }

    function burnText(s) {
        if (s < 0) return ""
        if (s < 60) return s + "s"
        var m = Math.floor(s / 60), ss = s % 60
        return m + ":" + (ss < 10 ? "0" + ss : ss)
    }
    // GIFs (animated MPEG4 and real .gif files) are not shown on the phone: we just flag them.
    property bool isGif: model.mediaKind == "gif"
    property bool isPhoto: model.mediaKind == "photo" || model.mediaKind == "sticker"
    property bool hasFileRow: model.mediaKind != "" && !isPhoto && !isGif

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
                font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
                color: platformStyle.colorNormalMid
            }
        }

        Label {
            width: parent.width - 4 * platformStyle.paddingLarge
            anchors.horizontalCenter: parent.horizontalCenter
            visible: model.service
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
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
                    if (!isPhoto) return
                    if (model.mediaState == "ready") {
                        // Full-resolution size is downloaded - open it.
                        root.openImage(model.localPath, index)
                    } else if (!model.previewLoaded) {
                        // Thumbnail preview mode: the photo is still the blurred placeholder.
                        // First tap just loads the inline preview (no full download).
                        chat.loadPreview(index)
                    } else {
                        // Preview is loaded: open it instantly, and fetch the full size in the
                        // background (so it opens full-res next time and "Save" saves the full image).
                        if (model.mediaThumb != "") root.openImage(model.mediaThumb, index)
                        if (model.mediaState == "idle") chat.downloadMedia(index)
                    }
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
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
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
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
                    color: "#b8d8f0"
                    width: parent.width
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
                Label {
                    id: replyLabel
                    visible: model.reply != ""
                    text: model.reply
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
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
                    // a "tap to load" chip while only the blurred placeholder is shown (Thumbnail mode)
                    Rectangle {
                        anchors.centerIn: parent
                        visible: isPhoto && !model.previewLoaded && model.mediaState != "loading" && model.mediaState != "failed"
                        width: tapHint.width + 2 * platformStyle.paddingMedium
                        height: tapHint.height + platformStyle.paddingSmall
                        radius: height / 2
                        color: "#99000000"
                        Label {
                            id: tapHint
                            anchors.centerIn: parent
                            text: qsTr("Tap to load")
                            color: "white"
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: model.mediaState == "failed"
                        text: qsTr("Failed - tap to retry")
                        color: "#ff9b9b"
                        font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
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
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
                            text: model.mediaKind == "voice"
                                ? (model.voicePlaying ? "■" : "▶")
                                : (model.mediaKind == "audio"
                                   ? (chat.audioRow == index ? chat.audioBuffer + "%" : "▶")
                                   : (model.mediaState == "ready" ? "O"
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
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
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
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.85
                        }
                    }
                }

                // -- an animated GIF: not played on the phone, shown as an "unsupported" placeholder --
                Row {
                    id: gifRow
                    visible: isGif
                    spacing: platformStyle.paddingMedium
                    Rectangle {
                        width: platformStyle.graphicSizeMedium
                        height: platformStyle.graphicSizeMedium
                        radius: 6
                        color: "#6a6a6a"
                        anchors.verticalCenter: parent.verticalCenter
                        Label { anchors.centerIn: parent; text: "GIF"; color: "white"; font.bold: true; font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.9 }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            text: qsTr("GIF file")
                            color: "white"
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale)
                            width: maxBubbleWidth - 2 * platformStyle.paddingMedium - platformStyle.graphicSizeMedium - platformStyle.paddingMedium
                            elide: Text.ElideRight
                        }
                        Label {
                            text: qsTr("not supported")
                            color: "#c0d4e6"
                            font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.85
                        }
                    }
                }

                Label {
                    id: bodyLabel
                    width: parent.width
                    text: root.linkify(model.body)
                    visible: model.body != ""
                    wrapMode: Text.Wrap
                    font.pixelSize: platformStyle.fontSizeMedium * root.fontScale
                    // The label spans the max bubble width while the bubble shrinks to the painted
                    // text; Qt would auto-align RTL (Persian/Arabic/Hebrew) text to the far right of
                    // that full width - outside the shrunken bubble, so it looks empty. Pin it left
                    // so short RTL text stays inside the bubble (LTR is unchanged).
                    horizontalAlignment: Text.AlignLeft
                    color: "white"
                    // Qt 4.7 only hit-tests links (onLinkActivated) for RichText - StyledText is the
                    // faster path but knows nothing about anchors. So a message with SEVERAL links is
                    // laid out as RichText and Qt reports exactly which one was tapped; the common
                    // 0/1-link case keeps the cheap StyledText path.
                    textFormat: root.linkCount(model.body) > 1 ? Text.RichText : Text.StyledText
                    // t.me/<name> opens that chat in Symbigram; anything else goes to the browser.
                    onLinkActivated: if (!app.openInternalLink(link)) Qt.openUrlExternally(link)
                    // Shortcut for a single link only. With several, the tap must reach the Text
                    // itself, and a long-press falls through to the bubble's own mouse area.
                    MouseArea {
                        anchors.fill: parent
                        enabled: root.linkCount(model.body) == 1
                        onClicked: { var u = root.firstLink(model.body); if (u != "" && !app.openInternalLink(u)) Qt.openUrlExternally(u) }
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
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.85
                    font.bold: true
                    color: "#ffb14e"
                }
                Label {
                    text: (model.edited ? qsTr("edited") + ", " : "") + model.timeText
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.85
                    color: "#c0c0c0"
                }
                Label {
                    visible: model.out
                    text: model.failed ? "!" : (model.pending ? "..." : (model.read ? "✓✓" : "✓"))
                    font.pixelSize: (platformStyle.fontSizeSmall * root.fontScale) * 0.85
                    color: model.failed ? "#ff9b9b" : (model.read ? "#8fd1ff" : "#c0c0c0")
                }
            }
        }
    }
}
