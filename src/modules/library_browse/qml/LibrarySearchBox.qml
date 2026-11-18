import QtQuick
import Auralis.Ui
import Auralis.Lib

// 工具列的搜尋框（畫面規格 S-02 工具列、S-07「搜尋」，AC-09-1）：以 UiRegistry 的工具列常駐項目登記，
// 每一頁都看得到。Ctrl+F 聚焦（SearchField）；輸入就切到 library/tracks 並套用（LibrarySearch.text，
// 去抖在服務裡）；有字時底線 amber、右側等寬顯示查詢耗時（「12 ms」）。
// Esc（或清除鈕）清掉字串時，如果是從別頁搜尋進來、而且還在搜尋結果頁，就回到進來前的頁面（Navigator.back）。
Item {
    id: box

    // 開始搜尋前所在的頁面；從 library/tracks 本身開始搜尋時是空字串
    property string returnPage

    // 核心可能把寬度縮到工具列的一半（T-28 定稿），搜尋框跟著變窄
    readonly property int fieldWidth: Theme.panelWidth - Theme.space12
    implicitWidth: fieldWidth + Theme.space3 + elapsed.implicitWidth
    implicitHeight: Theme.headerHeight

    function apply(text) {
        if (text.length > 0 && Navigator.currentPage !== "library/tracks") {
            box.returnPage = Navigator.currentPage
            Navigator.open("library/tracks")
        }
        LibrarySearch.text = text
        if (text.length === 0 && box.returnPage.length > 0) {
            box.returnPage = ""
            if (Navigator.currentPage === "library/tracks")
                Navigator.back()
        }
    }

    SearchField {
        id: field
        objectName: "librarySearchField"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(box.fieldWidth, box.width - Theme.space3 - elapsed.implicitWidth))
        placeholderText: qsTr("搜尋曲名、藝人、專輯")
        onTextChanged: {
            if (text !== LibrarySearch.text)
                box.apply(text)
        }
    }

    // 從別處改了搜尋字串（例：「清除搜尋與篩選」）時同步回搜尋框
    Connections {
        target: LibrarySearch
        function onCriteriaChanged() {
            if (field.text !== LibrarySearch.text)
                field.text = LibrarySearch.text
        }
    }

    Text {
        id: elapsed
        objectName: "librarySearchElapsed"
        visible: field.text.length > 0 && LibrarySearch.elapsedMs >= 0
        anchors.left: field.right
        anchors.leftMargin: Theme.space3
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("%1 ms").arg(LibrarySearch.elapsedMs)
        font: Theme.monoSmall
        color: Theme.inkMuted
    }
}
