import QtQuick
import Auralis.Ui

// 設定頁的「歌詞」分區（畫面規格 S-16，AC-16-3、AC-16-4）：線上抓取與另存 .lrc，即時生效。
// 值都在單例 Lyrics（設定 LYR/online 預設開、LYR/saveLrc 預設關）；點選只寫設定，再對回設定。
SettingsSection {
    id: section

    objectName: "lyricsSettingsSection"
    title: qsTr("歌詞")
    sectionId: "lyrics"

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
