import QtQuick

// ASIO 的商標標示（Steinberg 商標規則，T-104）：開啟或設定 ASIO 的畫面（設定頁的外掛分區與輸出分區、
// 輸出面板）與「關於」都放一份，寬度由使用處給。第 1 條要的 ASIO Compatible 標誌從已載入的 ASIO 外掛
// 資料夾讀（Steinberg 提供的原檔，依主題選深色用的白色透明版或淺色用的黑白版，T-105）；外掛沒載入時
// 這個位置只顯示文字的商標聲明（第 14、15 條的寫法）。標誌照原檔顯示：不改色、不裁切，只等比縮放。
Item {
    id: mark

    readonly property url logoSource: Theme.dark ? Plugins.asioLogoDark : Plugins.asioLogoLight
    readonly property bool hasLogo: logoSource.toString().length > 0

    objectName: "asioTrademark"
    implicitHeight: Math.max(logo.height, notice.implicitHeight)

    Image {
        id: logo
        objectName: "asioLogo"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: mark.hasLogo
        source: mark.logoSource
        // 標誌裡的「COMPATIBLE」小字要看得清楚：高度用 space12（48），向量圖依顯示大小重繪
        height: mark.hasLogo ? Theme.space12 : 0
        width: mark.hasLogo ? implicitWidth * height / Math.max(1, implicitHeight) : 0
        sourceSize.height: Theme.space12 * 2
        fillMode: Image.PreserveAspectFit
        Accessible.name: qsTr("ASIO Compatible 標誌")
    }
    Text {
        id: notice
        objectName: "asioTrademarkText"
        anchors.left: logo.right
        anchors.leftMargin: mark.hasLogo ? Theme.space3 : 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        // 條文的原文（英文），不翻譯
        text: "ASIO is a registered trademark of Steinberg Media Technologies GmbH."
        font: Theme.small
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
}
