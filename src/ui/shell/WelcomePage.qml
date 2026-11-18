import QtQuick

// 首次啟動歡迎（畫面規格 S-01，US-08）：音樂庫沒有音樂資料夾時取代內容區（側欄與播放列照常）。
// 內容區置中一欄：初始（標題、說明、「加入音樂資料夾」）→ 掃描中（刻度儀表、已掃描數／總數，
// AC-08-1；「在背景繼續」離開這頁，狀態列照樣顯示進度）→ 完成（加入幾首、有讀不到的檔案時
// 「查看掃描報告」，AC-08-5）。資料夾讀不到時由 Library 送通知列。資料都來自 Library。
Item {
    id: page

    // initial、scanning、done
    readonly property string phase: Library.scanning ? "scanning"
                                  : Library.addedPath.length > 0 ? "done" : "initial"
    readonly property int failed: Library.report.failed ?? 0

    // 畫面規格 S-01：一欄最寬 560
    readonly property int columnMaxWidth: 560

    // 「在背景繼續」：到音樂庫的第一頁；還沒有音樂庫的頁面時到設定（那裡也看得到資料夾與進度）
    function continueInBackground() {
        const pages = UiRegistry.sidebarPages
        Navigator.open(pages.length > 0 ? pages[0].id : "settings")
    }

    Column {
        id: column
        anchors.centerIn: parent
        width: Math.min(page.width - 2 * Theme.space8, page.columnMaxWidth)
        spacing: Theme.space6

        Text {
            objectName: "welcomeTitle"
            width: parent.width
            text: page.phase === "scanning" ? qsTr("正在掃描 %1").arg(Library.addedPath)
                  : page.phase === "done" ? qsTr("已加入 %1 首曲目")
                                            .arg(Number(Library.report.tracks ?? 0)
                                                 .toLocaleString(Qt.locale("en_US"), "f", 0))
                  : qsTr("加入你的音樂資料夾")
            font: page.phase === "initial" ? Theme.title : Theme.heading
            color: Theme.ink
            wrapMode: Text.WrapAnywhere
        }

        Text {
            visible: page.phase === "initial"
            width: parent.width
            text: qsTr("AuralisPlayer 只讀取音樂檔，不會修改或搬動任何檔案。")
            font: Theme.body
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }

        Button {
            objectName: "welcomeAdd"
            visible: page.phase === "initial"
            variant: "solid"
            text: qsTr("加入音樂資料夾")
            focus: visible
            onClicked: Library.chooseFolder()
        }

        // 掃描中
        Row {
            visible: page.phase === "scanning"
            spacing: Theme.space8

            RingGauge {
                value: Library.progress
                label: qsTr("掃描進度")
            }
            Readout {
                objectName: "welcomeProgress"
                anchors.verticalCenter: parent.verticalCenter
                label: qsTr("已掃描")
                // 總數在列出檔案之前還不知道
                value: Library.progressText.length > 0 ? Library.progressText : qsTr("正在列出檔案")
            }
        }
        Text {
            visible: page.phase === "scanning"
            width: parent.width
            text: qsTr("掃描期間可以照常瀏覽與播放。")
            font: Theme.body
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }
        Button {
            objectName: "welcomeContinue"
            visible: page.phase === "scanning"
            text: qsTr("在背景繼續")
            onClicked: page.continueInBackground()
        }

        // 完成但有讀不到的檔案
        Text {
            objectName: "welcomeFailed"
            visible: page.phase === "done" && page.failed > 0
            text: qsTr("%1 個檔案讀不到").arg(page.failed)
            font: Theme.body
            color: Theme.inkMuted
        }
        Button {
            objectName: "welcomeReport"
            visible: page.phase === "done" && page.failed > 0
            focus: visible
            text: qsTr("查看掃描報告")
            onClicked: report.open()
        }
    }

    ScanReportDialog {
        id: report
    }
}
