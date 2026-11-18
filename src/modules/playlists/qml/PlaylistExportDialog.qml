import QtQuick
import Auralis.Ui
import Auralis.Pl

// 匯出播放清單（畫面規格 S-10，US-14；T-54）：PlaylistExport.open(id) 之後打開。
// 標題「匯出「<名稱>」」；SegmentedControl「路徑」（絕對路徑／相對路徑，預設是上次選的）＋一行說明；
// 「選擇位置並匯出」（solid）開系統的儲存對話框，已存在的確認、寫檔與完成通知由 PlaylistExport 做；
// 「取消」關閉。
Dialog {
    id: dialog

    objectName: "playlistExportDialog"
    title: qsTr("匯出「%1」").arg(PlaylistExport.name)
    confirmText: qsTr("選擇位置並匯出")
    onAccepted: PlaylistExport.chooseAndExport(paths.currentIndex === 1)

    Connections {
        target: PlaylistExport
        function onOpened() {
            paths.currentIndex = PlaylistExport.relative ? 1 : 0
            dialog.open()
        }
    }

    contentItem: Column {
        spacing: Theme.space3

        Row {
            spacing: Theme.space4

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("路徑")
                font: Theme.body
                color: Theme.ink
            }
            SegmentedControl {
                id: paths
                objectName: "exportPathMode"
                label: qsTr("路徑")
                model: [{ text: qsTr("絕對路徑") }, { text: qsTr("相對路徑") }]
                onActivated: index => currentIndex = index
            }
        }
        Text {
            objectName: "exportPathHint"
            width: parent.width
            text: paths.currentIndex === 1 ? qsTr("相對於匯出檔所在的資料夾；不同磁碟機的曲目仍寫完整路徑")
                                           : qsTr("每一首寫完整的路徑（含磁碟機代號）")
            font: Theme.small
            color: Theme.inkMuted
            wrapMode: Text.Wrap
        }
    }
}
