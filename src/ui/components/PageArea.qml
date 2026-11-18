import QtQuick

// 頁面的錯誤邊界：以 Loader 載入頁面，載入失敗時顯示 ErrorBlock＋重試，其他區域照常。
// 頁面可以提供 property Component toolbar（放進工具列）、property string statusText（狀態列右邊，
// 等寬，例：「<n> 首符合」）與 property string statusTextLeft（狀態列左邊，例：選取說明）。
Item {
    id: area

    property url source
    readonly property Item item: loader.item as Item
    // 同一個物件，但不帶型別：頁面自訂的 toolbar、statusText、statusTextLeft 屬性從這裡取
    readonly property var page: loader.item

    clip: true

    Loader {
        id: loader
        anchors.fill: parent
        source: area.source
    }

    ErrorBlock {
        anchors.fill: parent
        visible: loader.status === Loader.Error
        onRetry: {
            loader.source = ""
            loader.source = Qt.binding(() => area.source)
        }
    }
}
