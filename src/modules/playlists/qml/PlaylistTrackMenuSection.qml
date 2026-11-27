pragma ComponentBehavior: Bound
import QtQuick
import Auralis.Ui
import Auralis.Pl

// 曲目右鍵選單裡本模組的區塊（US-11 AC-4；UiRegistry.addTrackMenuSection）：子選單「加入播放清單 ▸」，
// 列出現有的播放清單（依側欄順序）與最後的「新播放清單…」。區塊每次打開選單都重建、關閉後銷毀，
// 所以觸發的事交給單例 Playlists，不留在這裡。鍵盤：右鍵選單裡方向鍵到這一項、右鍵展開、Enter 執行。
Menu {
    id: sectionMenu

    required property string payloadJson
    property int trackCount

    readonly property var decoded: TrackDragCodec.decode(sectionMenu.payloadJson)
    // 只帶查詢條件（超過 5,000 首）的內容由 Playlists 在背景向音樂庫依條件取曲目
    readonly property bool usable: decoded.valid
                                   && (decoded.tracks.length > 0 || decoded.query.length > 0)

    title: qsTr("加入播放清單")
    enabled: sectionMenu.usable

    Instantiator {
        // 打開選單當下的清單，不跟著變：加入後清單會重讀，跟著變的話被按下的項目會在
        // triggered 處理到一半時被換掉，選單就不會整個關掉（區塊每次打開都重建，不會過時）
        Component.onCompleted: model = Playlists.playlists
        delegate: MenuItem {
            required property var modelData
            text: modelData.name
            onTriggered: Playlists.addTracks(modelData.id, sectionMenu.payloadJson)
        }
        onObjectAdded: (index, object) => sectionMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => sectionMenu.removeItem(object)
    }
    MenuItem {
        text: qsTr("新播放清單…")
        onTriggered: Playlists.createWithTracks(sectionMenu.payloadJson)
    }
}
