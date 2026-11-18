import QtQuick
import Auralis.Ui

// 有自己工具列的頁面（tst_toolbar，T-28）：像 S-03 的檢視方式、排序與欄位，確認常駐項目在它左邊、
// 窄視窗時頁面工具列仍放得下。
Rectangle {
    objectName: "fixturePage"
    color: Theme.bg
    property Component toolbar: Row {
        objectName: "fixturePageToolbar"
        spacing: Theme.space2

        SegmentedControl {
            anchors.verticalCenter: parent.verticalCenter
            model: ["封面牆", "清單", "詳細"]
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            text: "排序：專輯"
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            text: "欄位"
        }
    }
    property string statusText: "工具列測試頁"
    // T-29：狀態列左邊的說明（S-07 的選取說明）
    property string statusTextLeft: ""
}
