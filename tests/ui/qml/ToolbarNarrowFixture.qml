import QtQuick
import Auralis.Ui

// 工具列常駐項目（tst_toolbar，T-28）：order 比搜尋框小的窄項目，用來確認依 order 排列。
Rectangle {
    objectName: "fixtureToolbarNarrow"
    implicitWidth: Theme.space8
    implicitHeight: Theme.controlHeight
    color: Theme.surface
    border.width: Theme.hairline
    border.color: Theme.line
}
