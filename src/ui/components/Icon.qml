import QtQuick
import QtQuick.Shapes

// 圖示（介面規範 §6.2）：src/ui/icons/ 的 SVG 由 IconLibrary 轉成路徑，用 Shape 畫線，顏色依 tone 取 Theme 的 token
// （不經影像效果，圖檔本身不帶顏色）。size 16 或 20；16 以下的線寬在 24 畫板上用 1.5，畫出來仍是 1px。
// tone：ink（預設）、muted、faint（停用）、inverse（ink 實心底上）、amber、signal。
Item {
    id: icon

    property string name
    property int size: 16
    property string tone: "ink"

    // SVG 畫板的邊長（圖示規則固定 24×24）
    readonly property int artboard: 24
    readonly property color strokeColor: tone === "muted" ? Theme.inkMuted
                                         : tone === "faint" ? Theme.inkFaint
                                         : tone === "inverse" ? Theme.bg
                                         : tone === "amber" ? Theme.amber
                                         : tone === "signal" ? Theme.signal
                                         : Theme.ink

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Shape {
        width: icon.artboard
        height: icon.artboard
        scale: icon.size / icon.artboard
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: icon.strokeColor
            strokeWidth: icon.size <= 16 ? 1.5 : 1
            fillColor: Theme.transparent
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin

            PathSvg {
                path: IconLibrary.path(icon.name)
            }
        }
    }
}
