pragma ComponentBehavior: Bound
import QtQuick

// 設定頁的「音樂資料夾」分區（畫面規格 S-16，US-08）：資料夾清單（等寬路徑、曲目數、「移除」）、
// 「加入音樂資料夾」「重新掃描」、掃描中的進度（AC-08-1）、上次掃描時間與「查看掃描報告」（AC-08-5）。
// 移除前由 Library 開確認對話框。資料都來自 Library。
SettingsSection {
    id: section

    // 按「查看掃描報告」；設定頁開對話框
    signal reportRequested()

    function count(value) {
        return Number(value).toLocaleString(Qt.locale("en_US"), "f", 0)
    }

    title: qsTr("音樂資料夾")
    sectionId: "library"

    Repeater {
        model: Library.folders
        delegate: Item {
            id: folderRow
            required property var modelData
            required property int index
            objectName: "folderRow" + index
            width: section.width
            height: Theme.rowHeight

            Text {
                anchors.left: parent.left
                anchors.right: tracks.left
                anchors.rightMargin: Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                text: folderRow.modelData.path
                font: Theme.monoSmall
                color: Theme.ink
                elide: Text.ElideMiddle
            }
            Text {
                id: tracks
                anchors.right: remove.left
                anchors.rightMargin: Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                visible: folderRow.modelData.trackCount >= 0
                text: qsTr("%1 首").arg(section.count(folderRow.modelData.trackCount))
                font: Theme.monoSmall
                color: Theme.inkMuted
            }
            Button {
                id: remove
                objectName: "folderRemove" + folderRow.index
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("移除")
                Accessible.description: folderRow.modelData.path
                onClicked: Library.confirmRemove(folderRow.modelData.path)
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.hairline
                color: Theme.line
            }
        }
    }
    Text {
        visible: !Library.hasFolders
        text: qsTr("還沒有音樂資料夾。加入後會在背景建立音樂庫。")
        font: Theme.body
        color: Theme.inkMuted
    }

    Row {
        spacing: Theme.space2
        Button {
            objectName: "settingsAddFolder"
            variant: "solid"
            text: qsTr("加入音樂資料夾")
            onClicked: Library.chooseFolder()
        }
        Button {
            objectName: "settingsRescan"
            enabled: Library.hasFolders && !Library.scanning
            text: qsTr("重新掃描")
            onClicked: Library.rescan()
        }
    }

    // 掃描中：進度（已掃描數／總數）；掃描期間其他設定照常可用
    Column {
        visible: Library.scanning
        width: section.width
        spacing: Theme.space2
        ProgressLine {
            width: parent.width
            value: Library.progress
            indeterminate: Library.totalCount === 0
            label: qsTr("掃描進度")
        }
        Text {
            text: Library.progressText.length > 0 ? qsTr("已掃描 %1").arg(Library.progressText)
                                                  : qsTr("正在列出檔案")
            font: Theme.monoSmall
            color: Theme.inkMuted
        }
    }

    // 上次掃描時間與掃描報告
    Item {
        visible: Library.report.hasReport === true
        width: section.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("上次掃描 %1").arg(Library.report.finishedText ?? "")
                  + ((Library.report.failed ?? 0) > 0
                     ? qsTr("，%1 個檔案讀不到").arg(Library.report.failed) : "")
            font: Theme.small
            color: Theme.inkMuted
        }
        Button {
            objectName: "settingsReport"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("查看掃描報告")
            onClicked: section.reportRequested()
        }
    }
}
