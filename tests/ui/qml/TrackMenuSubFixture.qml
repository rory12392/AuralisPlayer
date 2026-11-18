import QtQuick
import Auralis.Ui

// 曲目右鍵選單的模組區塊（tst_track_menu）：子選單，像 PL 的「加入播放清單 ▸」。
Menu {
    required property string payloadJson
    objectName: "fixtureSub"
    title: "加入播放清單"
    MenuItem { text: "通勤" }
    MenuItem { text: "新增播放清單" }
}
