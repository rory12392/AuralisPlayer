import QtQuick
import Auralis.Ui

// 曲目右鍵選單的模組區塊（tst_track_menu）：一段裡有好幾個項目，像 LIB 的「前往專輯」「前往藝人」。
TrackMenuGroup {
    objectName: "fixtureGroup"
    MenuItem { objectName: "fixtureGroupA"; text: "前往專輯" }
    MenuItem { objectName: "fixtureGroupB"; text: "前往藝人" }
}
