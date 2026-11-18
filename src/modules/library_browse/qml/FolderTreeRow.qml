import QtQuick
import Auralis.Ui

// 資料夾樹的一列（S-06）：依深度縮排、有子資料夾時前面是展開／收合箭頭（點箭頭切換）、
// 資料夾名稱；根（使用者加入的音樂資料夾）顯示完整路徑，用等寬字。磁碟上找不到時文字轉 inkMuted、
// 右側寫「找不到資料夾」。選取是 surfaceRaised 底（同 SidebarItem）。
// 共用元件沒有樹（見模組規格整合待辦），先在本模組做：照封面牆的格子用 Item＋HoverHandler＋TapHandler，
// 鍵盤由頁面的 ListView 處理，列不取得焦點。
// T-25：整列也是拖放來源（根是 TrackDragArea，內容由頁面給：整個資料夾含子資料夾）；滑鼠右鍵發 menuRequested。
TrackDragArea {
    id: control

    property string text
    // 名稱同 LibraryFolders.tree 的角色：當 ListView 的 delegate 時由模型直接填入
    required property int depth
    required property bool hasChildren
    required property bool expanded
    required property bool missing
    required property bool root
    required property string path
    property bool current: false

    signal clicked()
    signal doubleClicked()
    signal toggleRequested()
    signal menuRequested()

    implicitWidth: 320
    implicitHeight: Theme.rowHeightCompact
    Accessible.role: Accessible.TreeItem
    Accessible.name: [root ? path : text, missing ? qsTr("找不到資料夾") : "",
                      hasChildren ? (expanded ? qsTr("已展開") : qsTr("已收合")) : ""]
                     .filter(part => part.length > 0).join(" ")

    HoverHandler {
        id: hover
    }
    TapHandler {
        onTapped: control.clicked()
        onDoubleTapped: control.doubleClicked()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: control.menuRequested()
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius0
        color: control.current || hover.hovered ? Theme.surfaceRaised : Theme.transparent
    }

    Icon {
        id: chevron
        x: Theme.space3 + control.depth * Theme.space4
        anchors.verticalCenter: parent.verticalCenter
        visible: control.hasChildren
        name: control.expanded ? "chevron-down" : "chevron-right"
        tone: "muted"
        size: 16

        TapHandler {
            // 點箭頭切換展開（同時也點到這一列，會選取它）
            onTapped: control.toggleRequested()
        }
    }
    Text {
        anchors.left: chevron.right
        anchors.leftMargin: Theme.space1
        anchors.right: missingText.left
        anchors.rightMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        text: control.root ? control.path : control.text
        font: control.root ? Theme.monoSmall : Theme.body
        color: control.missing ? Theme.inkMuted : Theme.ink
        elide: control.root ? Text.ElideMiddle : Text.ElideRight
    }
    Text {
        id: missingText
        anchors.right: parent.right
        anchors.rightMargin: Theme.space3
        anchors.verticalCenter: parent.verticalCenter
        text: control.missing ? qsTr("找不到資料夾") : ""
        font: Theme.small
        color: Theme.inkMuted
    }
}
