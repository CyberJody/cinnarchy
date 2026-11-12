import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180; height: 960
    minimumWidth: 900; minimumHeight: 560
    visible: true
    title: qsTr("Omarchy Theme Picker")
    color: "#101419"
    Material.theme: Material.Dark
    Material.accent: win.paletteColors.accent || theme.accent
    property var chosen: backend.selected
    property var paletteColors: chosen.roles || ({bg:"#1e1e2e",panel:"#161622",surface:"#313244",fg:"#cdd6f4",accent:"#89b4fa",on_accent:"#161622",border:"#45475a",muted:"#9399b2"})
    property var visibleThemes: backend.themes.filter(function(t) {
        return (modeFilter.currentIndex === 0 || t.mode === (modeFilter.currentIndex === 1 ? "dark" : "light"))
    })
    property int corner: theme.radius
    property bool noBackgrounds: (chosen.wallpapers || []).length === 0
    property var displayOptions: [{id:"all",label:qsTr("All displays · same background")}].concat(backend.displays.map(function(s){return {id:s.id,label:s.name+" · "+s.width+" × "+s.height}}))
    function revealSelection() {
        for (let i = 0; i < visibleThemes.length; ++i)
            if (visibleThemes[i].slug === backend.selectedSlug) { themeList.positionViewAtIndex(i,ListView.Contain); break }
    }
    // Material's corner scale arrived in Qt 6.5; older Qt keeps its default corners.
    function applyRounding() { if ("roundedScale" in win.Material) win.Material.roundedScale = theme.radius }
    Connections { target: backend; function onSelectionChanged() { Qt.callLater(win.revealSelection) } }
    Connections { target: theme; function onChanged() { win.applyRounding() } }
    Component.onCompleted: { win.applyRounding(); Qt.callLater(win.revealSelection) }

    // One look for every secondary button, sharp-cornered like Omarchy unless the desktop rounds.
    component SoftButton: Button {
        id: soft
        property bool quiet: false
        font.pixelSize: 12
        topInset: 0; bottomInset: 0
        leftPadding: 14; rightPadding: 14; topPadding: 9; bottomPadding: 9
        background: Rectangle {
            implicitHeight: 36
            radius: theme.radius
            color: soft.down ? "#303c49" : soft.hovered ? "#27323e" : soft.quiet ? "transparent" : "#1d2630"
            border.width: soft.quiet && !soft.visualFocus ? 0 : 1
            border.color: soft.visualFocus ? soft.Material.accent : "#34404d"
            opacity: soft.enabled ? 1 : 0.55
        }
        contentItem: Label {
            text: soft.text; font: soft.font
            color: !soft.enabled ? "#74808d" : soft.quiet ? "#9fb0c0" : "#e6edf4"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
    }

    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: backend.reload() }
    // Desktop shortcuts stay off while the Get more themes dialog takes typing.
    property bool shortcutsActive: !getThemes.visible
    // Space applies the theme, unless a button, checkbox or list box has keyboard focus and needs it.
    // A card clicked with the mouse only picks, so Space still applies; one reached with Tab is pressed.
    property bool spaceApplies: shortcutsActive && (!activeFocusItem || activeFocusItem === body
                                                    || (activeFocusItem.picksOnly === true && !activeFocusItem.visualFocus))
    Shortcut { sequence: "Ctrl+Return"; context: Qt.ApplicationShortcut; enabled: win.shortcutsActive && !backend.busy; onActivated: backend.apply() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.spaceApplies && !backend.busy; onActivated: backend.apply() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: win.shortcutsActive && backend.canUndo && !backend.busy; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.ApplicationShortcut; enabled: win.shortcutsActive && backend.canRedo && !backend.busy; onActivated: backend.redo() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.shortcutsActive && !backend.busy; onActivated: win.close() }

    Item {
        id: body
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        focus: true
        RowLayout {
            anchors.fill: parent; spacing: 0
            Rectangle {
                Layout.preferredWidth: 258; Layout.fillHeight: true
                color: "#151b22"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 12
                    RowLayout {
                        Layout.topMargin: 8; spacing: 10
                        Image { source: "qrc:/cinnarchy.svg"; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
                        ColumnLayout { spacing: 1
                            Label { text: "OMARCHY"; font.pixelSize: 15; font.bold: true; font.letterSpacing: 2; color: "#f4f5f7" }
                            Label { text: qsTr("for Cinnamon"); font.pixelSize: 12; color: "#94a1af" }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.topMargin: 12
                        Label { text: (translator.language, qsTr("%n themes", "", win.visibleThemes.length)); color: "#94a1af"; font.pixelSize: 12; Layout.fillWidth: true }
                        ComboBox { id: modeFilter; objectName: "modeFilter"; Layout.preferredWidth: 112; model: [qsTr("All"), qsTr("Dark"), qsTr("Light")]; font.pixelSize: 12; Accessible.name: qsTr("Theme mode") }
                    }
                    ListView {
                        id: themeList; objectName: "themeList"; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
                        model: win.visibleThemes; spacing: 5; clip: true
                        ScrollBar.vertical: ScrollBar { }
                        delegate: ItemDelegate {
                            id: card
                            required property var modelData
                            readonly property bool picksOnly: true
                            focusPolicy: Qt.StrongFocus
                            width: ListView.view.width; height: 72
                            enabled: !backend.busy
                            Accessible.name: modelData.title
                            highlighted: backend.selectedSlug === modelData.slug
                            onClicked: backend.selectTheme(modelData.slug)
                            background: Rectangle {
                                color: card.highlighted ? "#27323e" : card.hovered ? "#1e2832" : "transparent"
                                radius: win.corner; border.width: card.highlighted || card.visualFocus ? 1 : 0
                                border.color: card.visualFocus ? "#e6edf4" : card.modelData.roles.accent
                            }
                            contentItem: RowLayout {
                                spacing: 11
                                Rectangle {
                                    Layout.preferredWidth: 62; Layout.preferredHeight: 46; color: card.modelData.roles.bg; clip: true
                                    Image { anchors.fill: parent; source: card.modelData.thumbnail || ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 150 }
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 6; color: card.modelData.roles.accent }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 3
                                    Label { text: card.modelData.title; color: "#eef2f7"; font.pixelSize: 13; font.bold: card.highlighted; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: backend.activeName === card.modelData.name ? qsTr("Active") : card.modelData.mode === "light" ? qsTr("Light") : qsTr("Dark"); color: backend.activeName === card.modelData.name ? card.modelData.roles.accent : "#92a0af"; font.pixelSize: 11 }
                                }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: themeList.count === 0; text: qsTr("No matching themes"); color: "#94a1af" }
                    }
                    SoftButton { objectName: "getThemesButton"; Layout.fillWidth: true; text: qsTr("Get more themes…"); onClicked: getThemes.open() }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        SoftButton {
                            objectName: "reportButton"; quiet: true; leftPadding: 6; rightPadding: 6
                            text: qsTr("Report a problem") + " ↗"; Accessible.name: qsTr("Report a problem")
                            Accessible.description: qsTr("Opens the project's GitHub page in your browser")
                            ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: qsTr("Opens the project's GitHub page in your browser")
                            onClicked: Qt.openUrlExternally(backend.reportUrl())
                        }
                        Item { Layout.fillWidth: true }
                        Label { objectName: "versionLabel"; visible: backend.version !== ""; text: "v" + backend.version; color: "#6f7c89"; font.pixelSize: 11 }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#29323d" }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.margins: 26; spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Label { text: qsTr("One theme for all screens. Backgrounds can differ."); color: "#92a0af"; font.pixelSize: 12 }
                        Label { text: win.chosen.title || qsTr("Choose a theme"); color: "#f5f7fa"; font.pixelSize: 32; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Label { text: win.chosen.mode === "light" ? qsTr("LIGHT") : qsTr("DARK"); color: win.paletteColors.accent; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
                }
                // Everything between the title and the action row scrolls when the window is short,
                // so Apply and Undo stay on screen on small laptops.
                Flickable {
                    id: scroller; objectName: "optionsScroller"
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120; clip: true
                    readonly property bool scrolls: contentHeight > height + 1
                    contentWidth: width
                    contentHeight: Math.max(height, optionsColumn.implicitHeight - (preview.Layout.preferredHeight - preview.Layout.minimumHeight))
                    interactive: scrolls; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: scroller.scrolls ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                    ColumnLayout {
                        id: optionsColumn
                        width: scroller.width - (scroller.scrolls ? 16 : 0); height: scroller.contentHeight; spacing: 12
                        RowLayout {
                            Layout.fillWidth: true; spacing: 12
                            ComboBox {
                                id: displayCombo; objectName: "displayCombo"; Layout.fillWidth: true
                                model: win.displayOptions; textRole: "label"; valueRole: "id"; font.pixelSize: 12
                                currentIndex: Math.max(0,win.displayOptions.findIndex(function(s){return s.id===backend.targetDisplay}))
                                onActivated: backend.targetDisplay=currentValue
                                Accessible.name: qsTr("Wallpaper display")
                            }
                            SoftButton { objectName: "addMediaButton"; text: qsTr("Add image / video…"); onClicked: backend.chooseMedia(); enabled: !backend.busy }
                        }
                        ListView {
                            id: screenChoices; objectName: "displayAssignments"; Layout.fillWidth: true; Layout.preferredHeight: 66
                            visible: backend.displays.length>1; orientation: ListView.Horizontal; spacing: 10; clip: true
                            model: backend.displayAssignments
                            delegate: ItemDelegate {
                                id: screenCard; required property var modelData
                                readonly property bool picksOnly: true
                                focusPolicy: Qt.StrongFocus
                                width: Math.max(195,(screenChoices.width-10)/2); height: 62; enabled: !backend.busy
                                onClicked: backend.targetDisplay=modelData.id
                                Accessible.name: qsTr("Display: %1").arg(modelData.name)
                                background: Rectangle { color: backend.targetDisplay===screenCard.modelData.id?"#27323e":"#19212a"; radius: win.corner; border.width: backend.targetDisplay===screenCard.modelData.id||screenCard.visualFocus?1:0; border.color: screenCard.visualFocus?"#e6edf4":win.paletteColors.accent }
                                contentItem: RowLayout { spacing: 10
                                    Image { Layout.preferredWidth: 60; Layout.preferredHeight: 42; source: screenCard.modelData.thumbnail||""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 120; clip: true }
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Label { Layout.fillWidth: true; text: screenCard.modelData.name; color: "#dfe7ef"; font.pixelSize: 11; elide: Text.ElideRight }
                                        Label { Layout.fillWidth: true; text: (screenCard.modelData.animated?"▶ ":"")+(screenCard.modelData.title||qsTr("Choose background")); color: "#94a1af"; font.pixelSize: 11; elide: Text.ElideRight }
                                    }
                                }
                            }
                        }
                        DesktopPreview {
                            id: preview; objectName: "desktopPreview"
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 170; Layout.preferredHeight: 300
                            colors: win.paletteColors; wallpaper: backend.wallpaperUrl; fit: backend.pictureOption
                            media: backend.wallpaperMedia; playAnimations: !backend.animationsPaused; speed: backend.playbackSpeed
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: (backend.wallpaperMedia.animated?"▶ ":"")+(backend.wallpaperName || qsTr("No backgrounds found")); color: "#d8e0e9"; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                            Row { spacing: 5
                                Repeater { model: [win.paletteColors.bg,win.paletteColors.surface,win.paletteColors.accent,win.paletteColors.fg]
                                    Rectangle { required property string modelData; width: 15; height: 15; color: modelData; radius: Math.min(3,win.corner); border.color: "#607080"; border.width: 1 }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: (translator.language, qsTr("%n backgrounds", "", (win.chosen.wallpapers || []).length)); color: "#92a0af"; font.pixelSize: 12; Layout.fillWidth: true }
                            ComboBox {
                                id: fitCombo; objectName: "fitCombo"; Layout.preferredWidth: 150; font.pixelSize: 12
                                model: backend.targetDisplay==="all"?[qsTr("Fill screen"), qsTr("Fit image"), qsTr("Span screens"), qsTr("Centered")]:[qsTr("Fill screen"), qsTr("Fit image"), qsTr("Centered")]
                                property var options: backend.targetDisplay==="all"?["zoom","scaled","spanned","centered"]:["zoom","scaled","centered"]
                                currentIndex: Math.max(0, options.indexOf(backend.pictureOption))
                                onActivated: backend.pictureOption = options[currentIndex]
                                Accessible.name: qsTr("Wallpaper fit")
                            }
                        }
                        ListView {
                            id: backgrounds; objectName: "wallpaperList"
                            Layout.fillWidth: true; Layout.preferredHeight: 83; visible: !win.noBackgrounds
                            orientation: ListView.Horizontal; spacing: 9; clip: true
                            model: win.chosen.wallpapers || []; currentIndex: backend.wallpaperIndex
                            ScrollBar.horizontal: ScrollBar { }
                            delegate: ItemDelegate {
                                id: thumb
                                required property int index
                                required property var modelData
                                readonly property bool picksOnly: true
                                focusPolicy: Qt.StrongFocus
                                width: 105; height: 76; padding: 3
                                enabled: !backend.busy
                                Accessible.name: qsTr("Background: %1").arg(modelData.title)
                                onClicked: backend.selectWallpaper(index)
                                background: Rectangle { color: "transparent"; border.width: backend.wallpaperIndex === thumb.index ? 2 : thumb.visualFocus ? 1 : 0; border.color: thumb.visualFocus ? "#e6edf4" : win.paletteColors.accent; radius: win.corner }
                                contentItem: Column {
                                    spacing: 5
                                    Rectangle { width: 99; height: 49; color: "#222d38"; clip: true
                                        Image { anchors.fill: parent; source: thumb.modelData.thumbnail||""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 210 }
                                        Text { visible: !!thumb.modelData.animated; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3; text: "▶"; color: "white"; style: Text.Outline; styleColor: "black"; font.pixelSize: 16 }
                                    }
                                    Label { width: 99; text: thumb.modelData.title; color: backend.wallpaperIndex === thumb.index ? "#f4f5f7" : "#94a1af"; font.pixelSize: 10; elide: Text.ElideRight }
                                }
                            }
                        }
                        // Without Omarchy installed there are no backgrounds yet; offer its official ones.
                        Rectangle {
                            objectName: "backgroundsMissing"
                            Layout.fillWidth: true; Layout.preferredHeight: 83; visible: win.noBackgrounds && win.chosen.slug !== undefined
                            color: "#19212a"; radius: win.corner; border.width: 1; border.color: "#29323d"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 12
                                Label {
                                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#c3ced9"; font.pixelSize: 12
                                    text: (win.chosen.bundled ? qsTr("Omarchy's backgrounds for this theme aren't installed yet.") : qsTr("This theme has no backgrounds of its own."))
                                          + (backend.wallpaperUrl !== "" ? " " + qsTr("Your current background stays until you choose another.") : "")
                                }
                                BusyIndicator { running: backend.fetching; visible: running; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                                SoftButton { objectName: "downloadBackgroundsButton"; visible: !!win.chosen.bundled; enabled: !backend.fetching; text: qsTr("Download backgrounds"); onClicked: backend.downloadBackgrounds(false) }
                                SoftButton { objectName: "downloadAllBackgroundsButton"; visible: !!win.chosen.bundled && backend.themesWithoutBackgrounds > 1; enabled: !backend.fetching; text: qsTr("Download for all themes"); onClicked: backend.downloadBackgrounds(true)
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Download the backgrounds of every built-in theme (about 170 MB)") }
                            }
                        }
                        // How the chosen video, GIF or animated WebP plays on the desktop.
                        RowLayout {
                            objectName: "playbackControls"
                            Layout.fillWidth: true; spacing: 10; visible: !!backend.wallpaperMedia.animated
                            Label { text: qsTr("Speed"); color: "#92a0af"; font.pixelSize: 12 }
                            ComboBox {
                                id: speedCombo; objectName: "speedCombo"; Layout.preferredWidth: 118; font.pixelSize: 12
                                property var options: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2]
                                model: ["0.25×", "0.5×", "0.75×", qsTr("Normal"), "1.25×", "1.5×", "2×"]
                                currentIndex: Math.max(0, options.indexOf(backend.playbackSpeed))
                                onActivated: backend.playbackSpeed = options[currentIndex]
                                Accessible.name: qsTr("Animation speed")
                            }
                            Label { text: qsTr("Repeat"); color: "#92a0af"; font.pixelSize: 12; Layout.leftMargin: 8 }
                            ComboBox {
                                id: repeatCombo; objectName: "repeatCombo"; Layout.preferredWidth: 196; font.pixelSize: 12
                                property var options: [0, 5, 15, 30, 60, 300, 900, 3600, -1]
                                model: [qsTr("Right away"), qsTr("After 5 seconds"), qsTr("After 15 seconds"), qsTr("After 30 seconds"), qsTr("After 1 minute"),
                                        qsTr("After 5 minutes"), qsTr("After 15 minutes"), qsTr("After 1 hour"), qsTr("Never (play once)")]
                                currentIndex: Math.max(0, options.indexOf(backend.replayDelay))
                                onActivated: backend.replayDelay = options[currentIndex]
                                Accessible.name: qsTr("Repeat the animation")
                            }
                            Label { Layout.fillWidth: true; text: qsTr("Animated backgrounds loop silently and resume at login."); color: "#94a1af"; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#29323d" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            CheckBox { objectName: "wallpaperCheck"; text: qsTr("Apply wallpaper"); checked: backend.useWallpaper; onToggled: backend.useWallpaper=checked; font.pixelSize: 12 }
                            CheckBox { objectName: "iconsCheck"; text: qsTr("Apply matching icons"); checked: backend.useIcons; onToggled: backend.useIcons=checked; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            SoftButton { text: backend.animationsPaused?qsTr("Resume animation"):qsTr("Pause animation"); visible: backend.hasAnimations; onClicked: backend.pauseAnimations(); font.pixelSize: 11 }
                            SoftButton { text: qsTr("Stop animation"); visible: backend.hasAnimations; onClicked: backend.useSystemWallpaper(); font.pixelSize: 11 }
                        }
                        Label {
                            Layout.fillWidth: true; text: qsTr("Desktop · Applications · Window borders · %1").arg(win.chosen.icons || qsTr("No icon pack"))
                            color: "#94a1af"; font.pixelSize: 11; wrapMode: Text.Wrap
                        }
                        Label { Layout.fillWidth: true; visible: (win.chosen.iconNote || "") !== "" && backend.useIcons; text: win.chosen.iconNote || ""; color: "#edc68c"; font.pixelSize: 11; wrapMode: Text.Wrap }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    SoftButton { objectName: "undoButton"; text: qsTr("Undo"); font.pixelSize: 13; leftPadding: 20; rightPadding: 20; Layout.preferredHeight: 45; enabled: backend.canUndo && !backend.busy; onClicked: backend.undo() }
                    SoftButton { objectName: "redoButton"; text: qsTr("Redo"); font.pixelSize: 13; leftPadding: 20; rightPadding: 20; Layout.preferredHeight: 45; enabled: backend.canRedo && !backend.busy; onClicked: backend.redo() }
                    SoftButton { objectName: "restoreButton"; text: qsTr("Restore original"); quiet: true; Layout.preferredHeight: 45; enabled: backend.hasOriginal && !backend.busy; onClicked: backend.restoreOriginal() }
                    Item { Layout.fillWidth: true }
                    Button {
                        id: applyButton; objectName: "applyButton"; Layout.preferredWidth: 174; Layout.preferredHeight: 45
                        topInset: 0; bottomInset: 0
                        enabled: !backend.busy && backend.selectedSlug !== ""
                        Accessible.name: qsTr("Apply full theme")
                        text: backend.busy ? qsTr("Applying…") : qsTr("Apply full theme")
                        background: Rectangle {
                            color: applyButton.down ? Qt.darker(win.paletteColors.accent,1.12) : applyButton.hovered ? Qt.lighter(win.paletteColors.accent,1.06) : win.paletteColors.accent
                            radius: win.corner; opacity: applyButton.enabled ? 1 : 0.5
                            border.width: applyButton.visualFocus ? 2 : 0; border.color: win.paletteColors.fg
                        }
                        contentItem: Label { text: applyButton.text; color: win.paletteColors.on_accent; font.pixelSize: 13; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: backend.apply()
                    }
                }
                Label {
                    objectName: "statusMessage"; Layout.fillWidth: true; Layout.minimumHeight: 30
                    text: backend.message || qsTr("Select a theme and background, then apply the complete look.")
                    color: backend.error ? "#ff9b9b" : "#a9b8c8"; font.pixelSize: 12; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                }
            }
        }
    }
    Dialog {
        id: getThemes; objectName: "getThemesDialog"
        anchors.centerIn: parent; width: Math.min(win.width - 80, 860); height: Math.min(win.height - 60, 760); modal: true
        title: qsTr("Get more themes"); standardButtons: Dialog.Close
        background: Rectangle { color: "#151b22"; radius: win.corner; border.width: 1; border.color: "#29323d" }
        property var shownCommunity: backend.communityThemes.filter(function(t) {
            return t.name.toLowerCase().indexOf(communitySearch.text.toLowerCase()) >= 0
        })
        onOpened: if (backend.communityThemes.length === 0 && !backend.fetching) backend.loadCommunity()
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label { text: qsTr("Official Omarchy themes"); font.pixelSize: 14; font.bold: true }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: "#94a1af"; font.pixelSize: 12
                        text: qsTr("Downloads themes added to github.com/omacom/omarchy since this release, and prepares installed Omarchy themes for Cinnamon.") }
                }
                SoftButton { objectName: "fetchOfficialButton"; text: qsTr("Check for new themes"); enabled: !backend.fetching; onClicked: backend.fetchOfficial() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#29323d" }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Label { text: qsTr("Community themes from omarchy.org"); font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                TextField { id: communitySearch; Layout.preferredWidth: 220; placeholderText: qsTr("Search community themes…"); font.pixelSize: 12; Accessible.name: qsTr("Search community themes") }
            }
            GridView {
                id: communityGrid; objectName: "communityGrid"
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100; clip: true
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 200))); cellHeight: 156
                model: getThemes.shownCommunity
                ScrollBar.vertical: ScrollBar { }
                delegate: Item {
                    id: entry
                    required property var modelData
                    width: communityGrid.cellWidth; height: communityGrid.cellHeight
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 4
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 86; color: "#222d38"; radius: win.corner; clip: true
                            Image { anchors.fill: parent; source: entry.modelData.image; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 360 }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 4
                            Label { text: entry.modelData.name; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                            SoftButton {
                                font.pixelSize: 11; enabled: !backend.fetching; leftPadding: 10; rightPadding: 10; topPadding: 6; bottomPadding: 6
                                text: entry.modelData.installed ? qsTr("Update") : qsTr("Install")
                                Accessible.name: qsTr("Install %1").arg(entry.modelData.name)
                                onClicked: backend.installTheme(entry.modelData.url)
                            }
                        }
                    }
                }
                Label { anchors.centerIn: parent; visible: communityGrid.count === 0; color: "#94a1af"
                    text: backend.fetching ? qsTr("Loading community themes…") : backend.communityThemes.length === 0 ? qsTr("The community list is unavailable.") : qsTr("No matching themes") }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                TextField { id: repoUrl; objectName: "repoUrlField"; Layout.fillWidth: true; font.pixelSize: 12
                    placeholderText: qsTr("Or paste a theme's git URL, e.g. https://github.com/user/omarchy-name-theme")
                    Accessible.name: qsTr("Theme repository URL"); onAccepted: if (text.trim() !== "" && !backend.fetching) backend.installTheme(text) }
                SoftButton { text: qsTr("Install from URL"); enabled: repoUrl.text.trim() !== "" && !backend.fetching; onClicked: backend.installTheme(repoUrl.text) }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                BusyIndicator { running: backend.fetching; visible: running; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                Label { objectName: "fetchMessage"; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
                    text: backend.message; color: backend.error ? "#ff9b9b" : "#a9b8c8" }
            }
        }
    }
}
