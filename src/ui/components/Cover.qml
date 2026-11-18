import QtQuick

// 封面圖（介面規範 §3）：正方形，邊長 size。載入中是 Skeleton；沒有封面或讀不到時是
// surfaceRaised 方塊＋1px 圓線。依螢幕縮放比例取對應大小的縮圖。Accessible.name 是專輯名。
Item {
    id: cover

    property url source
    property int size: Theme.coverBar
    property string albumName

    readonly property bool hasImage: image.status === Image.Ready
    readonly property bool loading: image.status === Image.Loading

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Graphic
    Accessible.name: albumName

    Image {
        id: image
        anchors.fill: parent
        source: cover.source
        sourceSize.width: Math.ceil(cover.size * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(cover.size * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
    }

    Skeleton {
        anchors.fill: parent
        visible: cover.loading
        variant: "cover"
        size: cover.size
    }

    // 預設封面：surfaceRaised 方塊上的 1px 圓線
    Rectangle {
        anchors.fill: parent
        visible: !cover.hasImage && !cover.loading
        color: Theme.surfaceRaised
        radius: Theme.radius0

        Rectangle {
            anchors.centerIn: parent
            width: parent.width / 2
            height: width
            radius: Theme.radiusFull
            color: Theme.transparent
            border.width: Theme.hairline
            border.color: Theme.coverMark
        }
    }
}
