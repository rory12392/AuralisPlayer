import QtQuick
import Auralis.Ui

// tst_library 用的模組設定區塊：像模組那樣用共用元件排一個分區。
Column {
    spacing: Theme.space3

    Text {
        text: qsTr("測試模組")
        font: Theme.label
        color: Theme.inkMuted
    }
    Checkbox {
        objectName: "fixtureCheckbox"
        text: qsTr("測試選項")
    }
}
