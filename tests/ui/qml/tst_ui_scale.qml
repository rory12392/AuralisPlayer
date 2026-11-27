import QtQuick
import QtTest
import Auralis.Ui

// 設定頁「外觀」的介面縮放（S-16，AC-19-4）：選項文字、放不下的比例停用與說明、選了立即寫進設定、
// 和這次啟動的值不同時顯示「重新啟動後生效。」、換螢幕重新判斷。螢幕是假的（uiScaleProbe）。
Item {
    id: root
    width: 1440
    height: 900

    Loader {
        id: pageLoader
        anchors.fill: parent
        focus: true
    }

    TestCase {
        name: "UiScale"
        when: windowShown

        function item(name) {
            const found = findChild(pageLoader.item, name)
            verify(found !== null, name)
            return found
        }

        function load() {
            pageLoader.source = ""
            pageLoader.source = "qrc:/qt/qml/Auralis/Ui/shell/SettingsPage.qml"
            tryVerify(() => pageLoader.status === Loader.Ready)
        }

        function init() {
            UiScale.value = "system"
            uiScaleProbe.setMonitor(1.25, 1920, 1032)
            load()
        }

        function cleanup() {
            pageLoader.source = ""
            UiScale.value = "system"
        }

        function texts(control) {
            const list = []
            for (let i = 0; i < control.count; ++i)
                list.push(control.model[i].text)
            return list
        }

        function test_options_and_misfit_note() {
            const control = item("scaleControl")
            compare(texts(control), ["跟隨系統（125%）", "100%", "125%", "150%", "175%", "200%"])
            compare(control.currentIndex, 0)
            compare(control.displayText, "跟隨系統（125%）")
            verify(control.optionEnabled(3))
            verify(!control.optionEnabled(4))
            verify(!control.optionEnabled(5))
            compare(control.optionReason(4), "這個螢幕放不下")
            // 說明最小的放不下的比例（實體像素）
            const note = item("scaleNote")
            verify(note.visible)
            compare(note.text, "175% 時最小視窗是 1680×1120，這個螢幕只有 1920×1032 可用。")
            verify(!item("scaleRestart").visible)
        }

        function test_choose_writes_setting_and_shows_restart() {
            const control = item("scaleControl")
            control.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down) // 100%
            compare(UiScale.value, "1.0")
            verify(item("scaleRestart").visible)
            compare(item("scaleRestart").text, "重新啟動後生效。")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down) // 150%，再往下的都停用
            keyClick(Qt.Key_Down)
            compare(UiScale.value, "1.5")
            compare(control.currentIndex, 3)
            // 改回這次啟動的值：提示消失
            UiScale.value = "system"
            compare(control.currentIndex, 0)
            verify(!item("scaleRestart").visible)
        }

        function test_current_setting_misfit_and_other_screen() {
            // 設定是 200%，主視窗在放不下的螢幕：照樣顯示為選取、說明下次啟動的結果
            UiScale.value = "2.0"
            const control = item("scaleControl")
            compare(control.currentIndex, 5)
            compare(control.displayText, "200%")
            compare(item("scaleNote").text, "目前的設定 200% 在這個螢幕放不下，下次啟動會改用跟隨系統。")
            // 換到 4K（150%）：都放得下、沒有說明，跟隨系統的百分比跟著換
            uiScaleProbe.setMonitor(1.5, 3840, 2088)
            // 選項重建後選取不跑掉
            compare(control.currentIndex, 5)
            compare(control.displayText, "200%")
            verify(control.optionEnabled(5))
            verify(!item("scaleNote").visible)
            compare(control.model[0].text, "跟隨系統（150%）")
        }
    }
}
