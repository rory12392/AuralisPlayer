import QtQuick

// 設定頁的一個分區（畫面規格 S-16）：上方 label 標題與髮絲線，內容由上往下排。
// sectionId 是開頁參數 section 用的名稱（例：Navigator.open("settings", {section: "library"})）。
Column {
    id: section

    property string title
    property string sectionId
    default property alias content: body.data

    spacing: Theme.space3

    Text {
        text: section.title
        font: Theme.label
        color: Theme.inkMuted
    }
    Rectangle {
        width: section.width
        height: Theme.hairline
        color: Theme.line
    }
    Column {
        id: body
        width: section.width
        spacing: Theme.space3
    }
}
