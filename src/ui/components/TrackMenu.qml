pragma ComponentBehavior: Bound
import QtQml
import QtQuick
import QtQuick.Templates as T

// 曲目右鍵選單（介面規範 §3、畫面規格 S-03、S-04、S-07）。清單頁放一個，開的時候給被選取曲目的
// TrackDragPayload JSON（TrackDragCodec.encode）與曲目數：
//   滑鼠右鍵：popupAt(json, count)，在游標位置開。
//   選單鍵、Shift+F10（清單頁的 Keys 裡用 isOpenKey(event) 判斷）：popupFrom(json, count, item)，
//   開在 item 下緣，第一個能用的項目已經是目前項目（上下鍵移動、Enter 執行、Esc 關閉）。
// 內容依序：「播放」「接著播放」「加到佇列最後」→ 模組登記的區塊（UiRegistry.trackMenuSections，依 order）
// →「開啟檔案位置」（只有一首時；檔案在不在按下時才檢查）。只帶查詢條件（超過 5,000 首）時前三項照樣能用，
// 曲目由 Player 在背景向音樂庫取（T-62）。
// 模組區塊的介面（核心 UiRegistry）：區塊檔的根是 MenuItem（一個項目）、Menu（子選單）或
// TrackMenuGroup（好幾個項目）；建立時給 required property string payloadJson，有宣告
// property int trackCount 的也會給。每次打開都重新建立、關閉後銷毀。載不到的區塊只讓那一段不出現並寫日誌。
Menu {
    id: trackMenu

    /// 這次打開時的內容與曲目數（只帶查詢條件時曲目數由呼叫端給）。
    property string payloadJson
    property int trackCount
    readonly property var decoded: TrackDragCodec.decode(payloadJson)
    readonly property bool hasTracks: decoded.valid && decoded.tracks.length > 0
    /// 有曲目或只帶查詢條件（由 Player 在背景向音樂庫取曲目；T-62）：前三項能用。
    readonly property bool canQueue: hasTracks || (decoded.valid && decoded.query.length > 0)
    /// 只有一首而且有路徑時是它的路徑，否則空字串（「開啟檔案位置」）。開選單時不檢查檔案存在
    /// （離線磁碟會卡住介面），按下時 FileLocation 在背景檢查，不在就顯示錯誤通知。
    readonly property string singlePath: hasTracks && decoded.tracks.length === 1
                                         ? decoded.tracks[0].path : ""

    // 模組區塊建立出來的物件：{root, kind}，kind 是 item、menu 或 group
    property var sections: []
    property bool openedByKeyboard: false
    readonly property LoggingCategory log: LoggingCategory {
        name: "auralis.ui"
    }

    function popupAt(json, count) {
        prepare(json, count)
        openedByKeyboard = false
        popup()
    }

    function popupFrom(json, count, item) {
        prepare(json, count)
        openedByKeyboard = true
        popup(item, 0, item.height)
    }

    /// 清單頁開選單的按鍵：選單鍵、Shift+F10。
    function isOpenKey(event) {
        return event.key === Qt.Key_Menu
                || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)
    }

    function prepare(json, count) {
        clearSections()
        payloadJson = json
        trackCount = count >= 0 ? count : (decoded.valid ? decoded.tracks.length : 0)
        let index = 3
        const list = UiRegistry.trackMenuSections
        for (let i = 0; i < list.length; ++i)
            index = addSection(list[i], index)
    }

    // 把一個區塊放在第 index 項，回傳下一個區塊的位置；失敗時寫日誌、那一段不出現
    function addSection(section, index) {
        const component = Qt.createComponent(section.source)
        if (component.status !== Component.Ready) {
            console.warn(trackMenu.log, "曲目選單區塊載入失敗", section.id, component.errorString())
            return index
        }
        // 父物件給選單的清單（Item）：MenuItem 是 Item，掛在非 Item 底下 Qt 會警告
        const root = component.createObject(trackMenu.contentItem, { payloadJson: trackMenu.payloadJson })
        if (root === null) {
            console.warn(trackMenu.log, "曲目選單區塊建立失敗", section.id, component.errorString())
            return index
        }
        if ("trackCount" in root)
            // 只有宣告了 trackCount 的區塊才有（上一行已檢查），createObject 的靜態型別看不到
            root.trackCount = trackMenu.trackCount // qmllint disable missing-property
        if (root instanceof T.Menu) {
            trackMenu.insertMenu(index, root)
            sections.push({ root: root, kind: "menu" })
            return index + 1
        }
        if (root instanceof T.MenuItem) {
            trackMenu.insertItem(index, root)
            sections.push({ root: root, kind: "item" })
            return index + 1
        }
        if (root instanceof TrackMenuGroup) {
            const group = root as TrackMenuGroup
            for (let i = 0; i < group.items.length; ++i) {
                const item = group.items[i]
                if (item instanceof T.Menu)
                    trackMenu.insertMenu(index++, item)
                else if (item instanceof T.MenuItem)
                    trackMenu.insertItem(index++, item)
                else
                    console.warn(trackMenu.log, "曲目選單區塊有不是選單項目的內容", section.id)
            }
            sections.push({ root: root, kind: "group" })
            return index
        }
        console.warn(trackMenu.log, "曲目選單區塊的根不是 MenuItem、Menu 或 TrackMenuGroup", section.id)
        root.destroy()
        return index
    }

    function clearSections() {
        for (const entry of sections) {
            if (entry.kind === "menu") {
                trackMenu.removeMenu(entry.root)
            } else if (entry.kind === "item") {
                trackMenu.removeItem(entry.root)
            } else {
                for (let i = 0; i < entry.root.items.length; ++i) {
                    const item = entry.root.items[i]
                    if (item instanceof T.Menu)
                        trackMenu.removeMenu(item)
                    else if (item instanceof T.MenuItem)
                        trackMenu.removeItem(item)
                }
                entry.root.destroy()
            }
        }
        sections = []
    }

    function firstEnabledIndex() {
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item && item.enabled && item.visible)
                return i
        }
        return -1
    }

    onOpened: {
        if (openedByKeyboard)
            currentIndex = firstEnabledIndex()
    }
    onClosed: clearSections()

    MenuItem {
        text: qsTr("播放")
        shortcut: "Enter"
        enabled: trackMenu.canQueue
        onTriggered: Player.playTracks(trackMenu.payloadJson)
    }
    MenuItem {
        text: qsTr("接著播放")
        shortcut: "Shift+Enter"
        enabled: trackMenu.canQueue
        onTriggered: Player.enqueueNext(trackMenu.payloadJson)
    }
    MenuItem {
        text: qsTr("加到佇列最後")
        shortcut: "Ctrl+Enter"
        enabled: trackMenu.canQueue
        onTriggered: Player.enqueueLast(trackMenu.payloadJson)
    }
    // 模組區塊插在這裡（第 3 項起）
    MenuItem {
        text: qsTr("開啟檔案位置")
        visible: trackMenu.singlePath !== ""
        enabled: visible
        // 不出現時不佔高度（Menu 的清單照樣排它）
        height: visible ? implicitHeight : 0
        onTriggered: FileLocation.reveal(trackMenu.singlePath)
    }
}
