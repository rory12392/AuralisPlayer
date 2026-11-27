pragma ComponentBehavior: Bound
import QtQuick

// 設定頁「外觀」分區（畫面規格 S-16）：主題（AC-19-2，即時生效）與介面縮放（AC-19-4，重新啟動後生效）。
// 介面縮放的比例在主視窗目前所在的螢幕放不下（最小視窗換成實體像素超過可用範圍）時停用並說明；
// 主視窗換到別的螢幕時重新判斷。選了立即寫進 ui/scale，沒有「立即重新啟動」按鈕。
SettingsSection {
    id: appearance

    readonly property var themeModes: ["system", "void", "paper"]
    readonly property var scaleOptions: UiScale.options
    readonly property int scaleIndex: Math.max(0, scaleOptions.findIndex(o => o.value === UiScale.value))

    function percent(scale) {
        return qsTr("%1%").arg(Math.round(scale * 100))
    }

    function optionText(option) {
        return option.scale > 0 ? appearance.percent(option.scale)
                                : qsTr("跟隨系統（%1）").arg(appearance.percent(UiScale.systemScale))
    }

    // 停用的比例裡最小的那個（「<比例> 時最小視窗是…」說明這一個；比它大的一定也放不下）
    function firstMisfit() {
        return scaleOptions.find(o => !o.fits)
    }

    // 控制項下方的說明：目前的設定放不下時說下次啟動的結果，否則說最小的放不下的比例；都放得下時空字串
    function scaleNote() {
        const current = scaleOptions[scaleIndex]
        if (current !== undefined && !current.fits)
            return qsTr("目前的設定 %1 在這個螢幕放不下，下次啟動會改用跟隨系統。").arg(appearance.percent(current.scale))
        const misfit = appearance.firstMisfit()
        if (misfit === undefined)
            return ""
        return qsTr("%1 時最小視窗是 %2×%3，這個螢幕只有 %4×%5 可用。")
            .arg(appearance.percent(misfit.scale)).arg(misfit.width).arg(misfit.height)
            .arg(UiScale.available.width).arg(UiScale.available.height)
    }

    title: qsTr("外觀")
    sectionId: "appearance"

    Component.onCompleted: UiScale.refresh()

    Connections {
        target: appearance.Window.window
        function onScreenChanged() {
            UiScale.refresh()
        }
    }

    Item {
        width: parent.width
        height: Math.max(themeLabel.implicitHeight, themeControl.implicitHeight)
        Column {
            id: themeLabel
            anchors.left: parent.left
            anchors.right: themeControl.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("主題")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("跟隨系統時，Windows 切換深淺色後介面會跟著變。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        SegmentedControl {
            id: themeControl
            objectName: "themeControl"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("主題")
            model: [{ text: qsTr("跟隨系統") }, { text: qsTr("深色") }, { text: qsTr("淺色") }]
            currentIndex: Math.max(0, appearance.themeModes.indexOf(ThemeController.mode))
            // 寫進 settings.ini 的 ui/theme，重新啟動後保留（AC-19-2）
            onActivated: index => ThemeController.mode = appearance.themeModes[index]
        }
    }

    Item {
        width: parent.width
        height: Math.max(scaleLabel.implicitHeight, scaleControl.implicitHeight)
        Column {
            id: scaleLabel
            anchors.left: parent.left
            anchors.right: scaleControl.left
            anchors.rightMargin: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Text {
                text: qsTr("介面縮放")
                font: Theme.body
                color: Theme.ink
            }
            Text {
                width: parent.width
                text: qsTr("放大或縮小整個介面的文字、圖示與間距，重新啟動後生效。")
                font: Theme.small
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
        }
        Select {
            id: scaleControl
            objectName: "scaleControl"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("介面縮放")
            model: appearance.scaleOptions.map(o => ({
                text: appearance.optionText(o),
                enabled: o.fits,
                reason: o.fits ? "" : qsTr("這個螢幕放不下")
            }))
            // 選取跟著設定值：用鍵盤換值時 Select 自己設 currentIndex（蓋掉一般的繫結），換螢幕時選項
            // 整份重建、ComboBox 換 model 會把選取歸零，所以這幾個時候都放回設定值
            function sync() {
                currentIndex = Math.max(0, appearance.scaleOptions.findIndex(o => o.value === UiScale.value))
            }
            Component.onCompleted: sync()
            onModelChanged: sync()
            Connections {
                target: UiScale
                function onValueChanged() {
                    scaleControl.sync()
                }
            }
            // 立即寫進 settings.ini 的 ui/scale；這次啟動的比例不變
            onActivated: index => UiScale.value = appearance.scaleOptions[index].value
        }
    }
    // 控制項下方的兩行（和外掛分區的提示一樣）：放不下的說明、改了還沒重新啟動
    Text {
        objectName: "scaleNote"
        width: parent.width
        visible: text.length > 0
        text: appearance.scaleNote()
        font: Theme.small
        color: Theme.inkMuted
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.Wrap
    }
    Text {
        objectName: "scaleRestart"
        width: parent.width
        visible: UiScale.value !== UiScale.startupValue
        text: qsTr("重新啟動後生效。")
        font: Theme.small
        color: Theme.ink
        horizontalAlignment: Text.AlignRight
    }
}
