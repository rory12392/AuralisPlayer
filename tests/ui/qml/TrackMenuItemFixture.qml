import QtQuick
import Auralis.Ui

// 曲目右鍵選單的模組區塊（tst_track_menu）：單一項目，像 OUT 的「計算 ReplayGain」。
MenuItem {
    required property string payloadJson
    property int trackCount
    objectName: "fixtureSingle"
    text: "計算 ReplayGain"
}
