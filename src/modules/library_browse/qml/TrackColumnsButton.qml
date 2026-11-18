pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import Auralis.Ui
import Auralis.Lib
import "TrackColumnCatalog.js" as TrackColumnCatalog

// 頁面工具列的「欄位」（畫面規格 S-07，T-90，所有曲目與資料夾頁共用）：按鈕開選單，列出欄位目錄
// （預設欄＋可加的欄），勾選即增減，存 LibraryColumns（LIB/trackColumns）；曲名不能拿掉（停用）。
// 鍵盤：Tab 到按鈕、空白鍵或 Enter 開選單、上下鍵移動、空白鍵或 Enter 勾選（選單會關，焦點回按鈕）。
Button {
    id: button
    objectName: "trackColumnsButton"
    text: qsTr("欄位")

    readonly property var catalog: TrackColumnCatalog.catalog()
    readonly property var layout: TrackColumns.restore(catalog, LibraryColumns.saved)

    function toggle(id) {
        LibraryColumns.saved = TrackColumns.save(TrackColumnCatalog.toggled(button.layout, button.catalog, id))
    }

    onClicked: menu.popup(button, 0, button.height)

    Menu {
        id: menu
        objectName: "trackColumnsMenu"

        Instantiator {
            model: button.catalog
            delegate: MenuItem {
                id: item
                required property var modelData
                objectName: "trackColumn_" + modelData.id
                text: modelData.title
                checkable: true
                checked: TrackColumnCatalog.contains(button.layout, modelData.id)
                enabled: !TrackColumnCatalog.isLocked(modelData.id)
                leftPadding: Theme.space3 + box.boxSize + Theme.space2
                Accessible.role: Accessible.CheckBox
                // 勾選框是共用的 Checkbox，只負責畫：不取得焦點、不接滑鼠（點整列由選單項目處理）
                indicator: Checkbox {
                    id: box
                    x: Theme.space3
                    y: Math.round((item.height - Theme.lineBody) / 2)
                    checked: item.checked
                    enabled: item.enabled
                    activeFocusOnTab: false
                    focusPolicy: Qt.NoFocus
                    containmentMask: Item {}
                    Accessible.ignored: true
                }
                // checkable 的選單項目觸發時已經切換了 checked；以存檔為準重算（綁定會蓋回去）
                onTriggered: button.toggle(modelData.id)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
}
