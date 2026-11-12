import QtQuick
import QtMultimedia

Item {
    id: root
    property var media: ({})
    property string source: ""
    property string fit: "zoom"
    property bool playing: true
    property real speed: 1
    readonly property bool stillVideoPreview: GraphicsInfo.api === GraphicsInfo.Software
    clip: true
    Image {
        objectName: "mediaPoster"
        anchors.fill: parent
        visible: root.media.mediaType !== "animated-image"
        source: root.media.mediaType === "video" ? (root.media.thumbnail || "") : root.source
        fillMode: root.fit === "scaled" || root.fit === "centered" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true; sourceSize.width: 1400
    }
    AnimatedImage {
        anchors.fill: parent; visible: root.media.mediaType === "animated-image"
        source: visible ? root.source : ""; playing: root.playing && visible; speed: root.speed
        fillMode: root.fit === "scaled" || root.fit === "centered" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        asynchronous: true; cache: false
    }
    Loader {
        id: videoLoader; anchors.fill: parent; active: root.media.mediaType === "video" && !root.stillVideoPreview
        sourceComponent: Item {
            VideoOutput { id: output; anchors.fill: parent; fillMode: root.fit === "scaled" || root.fit === "centered" ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop }
            MediaPlayer {
                id: player; source: root.source; loops: MediaPlayer.Infinite; videoOutput: output; playbackRate: root.speed
                audioOutput: AudioOutput { muted: true }
                autoPlay: root.playing
                onErrorOccurred: videoError.text = qsTr("Preview unavailable: %1").arg(errorString)
            }
            Connections { target: root; function onPlayingChanged() { if(root.playing)player.play();else player.pause() } }
            Text { id: videoError; anchors.centerIn: parent; width: parent.width-30; wrapMode: Text.Wrap; color: "white"; style: Text.Outline; styleColor: "black"; font.pixelSize: 12 }
        }
    }
    Text {
        visible: root.media.mediaType === "video" && root.stillVideoPreview
        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
        text: qsTr("Still preview · animation plays on the desktop")
        color: "white"; style: Text.Outline; styleColor: "black"; font.pixelSize: 11
    }
}
