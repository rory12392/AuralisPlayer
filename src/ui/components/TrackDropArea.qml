import QtQuick

// 跨模組拖放曲目的目標（介面規範 §3）：放在播放清單的側欄項目、佇列面板等接收曲目的地方。
// 只接受 TrackDragArea 拖來的內容（MIME application/x-auralis-tracks）；containsDrag 時由使用的元件
// 顯示拖曳目標的樣子（例：SidebarItem 的 dropTarget）。放下時發出 tracksDropped(payload)：
// { json（TrackDragPayload 的 JSON，C++ 用 fromJson 讀）, tracks: [{path, subsong}], query, count, x, y }。
DropArea {
    id: area

    signal tracksDropped(var payload)

    keys: [TrackDragCodec.mimeType]

    onDropped: drop => {
        // 視窗內的拖曳（Drag.Internal）只帶 keys、不帶 mimeData，內容從來源元件取；
        // 從別的程式拖進來時才有 mimeData
        const source = drop.source as TrackDragArea
        const json = source ? source.payloadJson : drop.getDataAsString(TrackDragCodec.mimeType)
        const decoded = TrackDragCodec.decode(json)
        if (!decoded.valid) {
            drop.accepted = false
            return
        }
        drop.acceptProposedAction()
        area.tracksDropped({
            json: json,
            tracks: decoded.tracks,
            query: decoded.query,
            count: source ? source.count : decoded.tracks.length,
            x: drop.x,
            y: drop.y
        })
    }
}
