pragma ComponentBehavior: Bound
import QtQuick

// 掃描報告（畫面規格 S-17，AC-08-5）：讀數（曲目、新增、移除、讀不到、耗時）與讀不到的檔案清單
// （等寬路徑＋原因）；按鈕「關閉」（預設焦點、Esc）與「複製清單」（有讀不到的檔案時才有）。
// 資料來自 Library.report，掃描中打開也會跟著更新。
Dialog {
    id: dialog

    readonly property var report: Library.report
    readonly property var failures: report.failures ?? []

    // 數量用千分位（介面規範 §6.1）
    function count(value) {
        return Number(value ?? 0).toLocaleString(Qt.locale("en_US"), "f", 0)
    }

    objectName: "scanReportDialog"
    title: qsTr("掃描報告")
    cancelText: qsTr("關閉")
    confirmText: qsTr("複製清單")
    confirmVisible: failures.length > 0
    onAccepted: Library.copyFailures()

    contentItem: Column {
        spacing: Theme.space4

        Flow {
            width: parent.width
            spacing: Theme.space6

            Readout {
                label: qsTr("曲目")
                value: dialog.count(dialog.report.tracks)
            }
            Readout {
                label: qsTr("新增")
                value: dialog.count(dialog.report.added)
            }
            Readout {
                label: qsTr("移除")
                value: dialog.count(dialog.report.removed)
            }
            Readout {
                objectName: "reportFailed"
                label: qsTr("讀不到")
                value: dialog.count(dialog.report.failed)
            }
            Readout {
                label: qsTr("耗時")
                value: dialog.report.durationValue ?? ""
                unit: dialog.report.durationUnit ?? ""
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.hairline
            color: Theme.line
        }

        Text {
            visible: dialog.failures.length === 0
            text: qsTr("所有檔案都讀取成功。")
            font: Theme.body
            color: Theme.inkMuted
        }

        // 讀不到的檔案：最多顯示 8 列、其餘捲動
        ListView {
            id: list
            objectName: "reportFailures"
            visible: dialog.failures.length > 0
            width: parent.width
            height: Math.min(contentHeight, 8 * Theme.rowHeightCompact)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.failures
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: list; lineHeight: Theme.rowHeightCompact }

            delegate: Item {
                id: failure
                required property var modelData
                width: list.width
                height: Theme.rowHeightCompact

                Text {
                    anchors.left: parent.left
                    anchors.right: reason.left
                    anchors.rightMargin: Theme.space4
                    anchors.verticalCenter: parent.verticalCenter
                    text: failure.modelData.path
                    font: Theme.monoSmall
                    color: Theme.ink
                    elide: Text.ElideMiddle
                }
                Text {
                    id: reason
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    text: failure.modelData.reason
                    font: Theme.small
                    color: Theme.inkMuted
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: Theme.hairline
                    color: Theme.line
                }
            }
        }
    }
}
