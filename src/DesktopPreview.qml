import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property var colors: ({bg:"#1e1e2e", panel:"#161622", surface:"#313244", fg:"#cdd6f4", accent:"#89b4fa", on_accent:"#161622", border:"#45475a", muted:"#9399b2"})
    property string wallpaper: ""
    property string fit: "zoom"
    property var media: ({})
    property bool playAnimations: true
    color: colors.bg
    clip: true
    MediaBackdrop {
        anchors.fill: parent
        source: root.wallpaper
        media: root.media; fit: root.fit; playing: root.playAnimations
    }
    Rectangle {
        anchors.top: parent.top; width: parent.width; height: 29
        color: root.colors.panel; opacity: 0.97
        Row {
            anchors.left: parent.left; anchors.leftMargin: 13; anchors.verticalCenter: parent.verticalCenter; spacing: 17
            Text { text: "☰"; color: root.colors.accent; font.pixelSize: 14 }
            Text { text: qsTr("Menu"); color: root.colors.fg; font.pixelSize: 11 }
            Text { text: qsTr("Files"); color: root.colors.fg; font.pixelSize: 11 }
        }
        Text { anchors.centerIn: parent; text: "10:24"; color: root.colors.fg; font.pixelSize: 11 }
        Text { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; text: "●   ◖))   ▰"; color: root.colors.fg; font.pixelSize: 11 }
    }
    Rectangle {
        width: Math.min(390, parent.width * 0.63); height: Math.min(222, parent.height * 0.72)
        anchors.centerIn: parent; anchors.verticalCenterOffset: 11
        color: root.colors.bg; radius: 10; border.color: root.colors.border; clip: true
        Rectangle {
            x: 1; y: 1; width: parent.width - 2; height: 35; radius: 9; color: root.colors.panel
            Rectangle { anchors.bottom: parent.bottom; height: 10; width: parent.width; color: parent.color }
            Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Files"); color: root.colors.fg; font.pixelSize: 11; font.bold: true }
            Text { anchors.right: parent.right; anchors.rightMargin: 13; anchors.verticalCenter: parent.verticalCenter; text: "−   □   ×"; color: root.colors.fg; font.pixelSize: 12 }
        }
        Rectangle {
            x: 1; y: 36; width: 88; height: parent.height - 37; color: root.colors.panel
            Column {
                anchors.fill: parent; anchors.margins: 11; spacing: 14
                Text { text: qsTr("Home"); color: root.colors.accent; font.pixelSize: 10 }
                Text { text: qsTr("Documents"); color: root.colors.fg; font.pixelSize: 10 }
                Text { text: qsTr("Pictures"); color: root.colors.fg; font.pixelSize: 10 }
            }
        }
        Column {
            x: 105; y: 51; width: parent.width - 122; spacing: 15
            Text { text: qsTr("A place for everything"); color: root.colors.fg; font.pixelSize: 13; font.bold: true }
            Row {
                spacing: 13
                Repeater {
                    model: 3
                    Column {
                        spacing: 5
                        Rectangle { width: 43; height: 30; radius: 3; color: root.colors.accent; opacity: 0.85
                            Rectangle { x: 0; y: -4; width: 20; height: 8; radius: 2; color: root.colors.accent }
                        }
                        Text { text: [qsTr("Projects"), qsTr("Photos"), qsTr("Notes")][index]; color: root.colors.fg; font.pixelSize: 9 }
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: root.colors.border }
            Row {
                spacing: 8
                Rectangle { width: 66; height: 25; radius: 5; color: root.colors.surface
                    Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: root.colors.fg; font.pixelSize: 10 }
                }
                Rectangle { width: 66; height: 25; radius: 5; color: root.colors.accent
                    Text { anchors.centerIn: parent; text: qsTr("Open"); color: root.colors.on_accent; font.pixelSize: 10 }
                }
            }
        }
    }
    Rectangle {
        anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 12
        width: previewLabel.width + 18; height: 25; radius: 4; color: "#b0101419"
        Text { id: previewLabel; anchors.centerIn: parent; text: qsTr("Desktop preview"); color: "white"; font.pixelSize: 10 }
    }
}
