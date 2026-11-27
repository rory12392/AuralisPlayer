import QtQuick
import Auralis.Ui

// 設定頁的「歌詞與全螢幕」分區（畫面規格 S-16，AC-17-6、AC-16-3、AC-16-4）：全螢幕主題、
// 線上抓取與另存 .lrc，即時生效。值都在單例 Lyrics（設定 LYR/fullscreenTheme 預設深色、
// LYR/online 預設開、LYR/saveLrc 預設關）；點選只寫設定，再對回設定。
SettingsSection {
    id: section

    readonly property var fullscreenThemes: ["dark", "light", "follow"]

    objectName: "lyricsSettingsSection"
    title: qsTr("歌詞與全螢幕")
    sectionId: "lyrics"

    Item {
        width: section.width
        height: Math.max(themeLabel.implicitHeight, themeControl.implicitHeight)

        Column {
            id: themeLabel
            anchors.left: parent.left
            anchors.right: themeControl.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("全螢幕主題")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("全螢幕正在播放畫面的深淺色，和主程式的主題分開設定。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        SegmentedControl {
            id: themeControl
            objectName: "lyricsFullscreenTheme"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("全螢幕主題")
            model: [{ text: qsTr("深色") }, { text: qsTr("淺色") }, { text: qsTr("跟隨主程式") }]
            // 全螢幕頁的 T 鍵與切換鈕也會改：用 Binding，點選時控制項自己寫 currentIndex 也不會斷開
            Binding on currentIndex {
                value: section.fullscreenThemes.indexOf(Lyrics.fullscreenTheme)
            }
            onActivated: index => Lyrics.setFullscreenTheme(section.fullscreenThemes[index])
        }
    }

    Checkbox {
        objectName: "lyricsOnline"
        width: section.width
        text: qsTr("從 LRCLIB 抓取歌詞")
        description: qsTr("本機找不到歌詞時才會連線，只送出曲名、藝人、專輯與長度。")
        checked: Lyrics.onlineEnabled
        onToggled: {
            Lyrics.setOnlineEnabled(checked)
            checked = Qt.binding(() => Lyrics.onlineEnabled)
        }
    }

    Checkbox {
        objectName: "lyricsSaveLrc"
        width: section.width
        text: qsTr("把抓到的歌詞另存為同名 .lrc")
        description: qsTr("只新增 .lrc 檔，不修改音樂檔。")
        checked: Lyrics.saveLrc
        onToggled: {
            Lyrics.setSaveLrc(checked)
            checked = Qt.binding(() => Lyrics.saveLrc)
        }
    }
}
