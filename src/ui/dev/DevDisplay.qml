pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁「顯示與狀態」：Panel、Readout、StatusTag、RingGauge、ProgressLine、EmptyState、
// ErrorBlock、Skeleton、Cover（介面規範 §3 的順序）。
Flow {
    id: section

    readonly property int columnWidth: width >= 2 * Theme.panelWidth + Theme.space6
                                       ? Math.floor((width - Theme.space6) / 2) : width

    spacing: Theme.space6

    Panel {
        width: section.columnWidth
        title: "Panel"
        index: "Ctrl O"
        Text {
            width: parent.width
            text: qsTr("四個角標的髮絲線框；右上角只放有資訊的東西（快捷鍵、數量）。")
            font: Theme.body
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }
    }

    Panel {
        width: section.columnWidth
        title: "Readout"
        Row {
            spacing: Theme.space6
            Readout { label: qsTr("來源"); value: "FLAC 96 / 24" }
            Readout { label: qsTr("處理"); value: qsTr("無") }
            Readout { label: qsTr("送到 DAC"); value: "96"; unit: "kHz / 24 bit"; tone: "amber" }
        }
    }

    Panel {
        width: section.columnWidth
        title: "StatusTag"
        Column {
            spacing: Theme.space3
            Row {
                spacing: Theme.space6
                StatusTag { text: qsTr("位元完美"); variant: "amber" }
                StatusTag { text: qsTr("音量 85%"); variant: "neutral" }
            }
            Row {
                spacing: Theme.space6
                StatusTag { text: qsTr("裝置被其他程式使用中"); variant: "signal" }
                StatusTag { text: qsTr("未連線"); variant: "idle" }
            }
        }
    }

    Panel {
        width: section.columnWidth
        title: "RingGauge"
        Row {
            spacing: Theme.space6
            RingGauge { label: qsTr("首次掃描"); value: 0.31; size: Theme.space24 + Theme.space6 }
            RingGauge { label: qsTr("首次掃描"); value: 1; size: Theme.space24 + Theme.space6 }
        }
    }

    Panel {
        width: section.columnWidth
        title: "ProgressLine"
        Column {
            width: parent.width
            spacing: Theme.space4
            Text { text: qsTr("已掃描 12,480 / 40,112"); font: Theme.monoSmall; color: Theme.inkMuted }
            ProgressLine { width: parent.width; label: qsTr("掃描"); value: 12480 / 40112 }
            Text { text: qsTr("計算 ReplayGain"); font: Theme.monoSmall; color: Theme.inkMuted }
            ProgressLine { width: parent.width; label: qsTr("計算 ReplayGain"); indeterminate: true }
        }
    }

    Panel {
        width: section.columnWidth
        title: "EmptyState"
        EmptyState {
            width: parent.width
            title: qsTr("佇列是空的")
            description: qsTr("在曲目上按右鍵選「加到佇列最後」。")
            actionText: qsTr("加入音樂資料夾")
        }
    }

    Panel {
        width: section.columnWidth
        title: "ErrorBlock"
        ErrorBlock {
            width: parent.width
            message: qsTr("歌詞面板無法載入。")
        }
    }

    Panel {
        width: section.columnWidth
        title: "Skeleton"
        Column {
            width: parent.width
            spacing: Theme.space2
            Skeleton { width: parent.width; variant: "row"; delayed: false }
            Skeleton { width: parent.width; variant: "row"; delayed: false }
            Skeleton { variant: "cover"; size: Theme.space24; delayed: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Cover"
        Row {
            spacing: Theme.space4
            Cover { size: Theme.space24; albumName: qsTr("沒有封面的專輯") }
            Cover { size: Theme.coverBar; albumName: qsTr("沒有封面的專輯") }
            Cover { size: Theme.coverThumb; albumName: qsTr("沒有封面的專輯") }
        }
    }
}
