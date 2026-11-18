import QtQuick
import QtTest
import Auralis.Ui

// 播放列的音量（S-02，AC-02-7）：標籤「音量」、滑桿、數值；滑鼠點哪裡就是哪裡（不取級距）、左右鍵一次 5%；
// 音量不是 100 時輸出按鈕寫「音量 n%」（AC-02-3）；DSD 直通（假狀態）時滑桿停用並說明原因；
// 960 寬時不擠壞。Player 背後是假的 PlaybackService（fakePlayback）。
Item {
    id: root
    width: 1440
    height: 900

    AppShell {
        id: shell
        anchors.fill: parent
        focus: true
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    TestCase {
        name: "Volume"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function volumeCalls() {
            return fakePlayback.calls.filter(call => call.startsWith("setVolume "))
        }

        // 滑鼠在 x（滑桿自己的座標）時該有的百分比
        function percentAt(slider, x) {
            const travel = slider.availableWidth - slider.handle.width
            const fraction = (x - slider.leftPadding - slider.handle.width / 2) / travel
            return 100 * Math.max(0, Math.min(1, fraction))
        }

        function init() {
            root.width = 1440
            fakePlayback.reset(2, 0)
            fakePlayback.setOutput("", [], "")
            shell.forceActiveFocus()
        }

        function test_default_is_100() {
            const slider = item("volumeSlider")
            verify(slider.enabled)
            compare(slider.value, 100)
            compare(slider.Accessible.name, "音量")
            compare(item("volumeText").text, "100%")
        }

        // 點一下：音量是滑鼠的位置（四捨五入到整數百分比），不被取到 5 的倍數
        function test_pointer_sets_volume() {
            const slider = item("volumeSlider")
            const travel = slider.availableWidth - slider.handle.width
            for (const fraction of [0.62, 0.23, 0.87]) {
                fakePlayback.reset(2, 0)
                const x = Math.round(slider.handle.width / 2 + travel * fraction)
                mouseClick(slider, x, slider.height / 2)
                verify(volumeCalls().length >= 1)
                const expected = Math.round(percentAt(slider, x))
                compare(Player.volume, expected)
                verify(expected % 5 !== 0, "測試的落點不該剛好在級距上：" + expected)
                compare(item("volumeText").text, expected + "%")
                // 放開後接回服務的值
                compare(slider.value, expected)
            }
        }

        // 拖曳：每一步都立即生效
        function test_drag_applies_immediately() {
            const slider = item("volumeSlider")
            const travel = slider.availableWidth - slider.handle.width
            const y = slider.height / 2
            const from = Math.round(slider.handle.width / 2 + travel * 0.9)
            const to = Math.round(slider.handle.width / 2 + travel * 0.3)
            mousePress(slider, from, y)
            for (let i = 1; i <= 8; ++i) {
                const x = Math.round(from + (to - from) * i / 8)
                mouseMove(slider, x, y)
                compare(Player.volume, Math.round(percentAt(slider, x)))
            }
            mouseRelease(slider, to, y)
            compare(Player.volume, Math.round(percentAt(slider, to)))
            verify(volumeCalls().length >= 8)
        }

        function test_keys_step_5() {
            const slider = item("volumeSlider")
            slider.forceActiveFocus()
            keyClick(Qt.Key_Left)
            compare(Player.volume, 95)
            keyClick(Qt.Key_Left)
            compare(Player.volume, 90)
            keyClick(Qt.Key_Right)
            compare(Player.volume, 95)
            compare(slider.value, 95)
            // 到頂不再增加
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            compare(Player.volume, 100)
            // 服務那邊改了（例：之後的系統媒體控制），滑桿跟著
            fakePlayback.setVolume(42)
            compare(slider.value, 42)
            compare(item("volumeText").text, "42%")
        }

        // 音量不是 100：輸出按鈕的狀態是 neutral 並寫出「音量 n%」（原因由服務的輸出狀態帶來）
        function test_output_button_shows_volume() {
            const tag = item("outputStatusTag")
            fakePlayback.setOutput("WASAPI 獨占", [], "")
            compare(tag.text, "位元完美")
            compare(tag.variant, "amber")
            fakePlayback.setOutput("WASAPI 獨占", ["音量 60%"], "")
            compare(tag.text, "音量 60%")
            compare(tag.variant, "neutral")
            compare(Output.processingText, "音量 60%")
            fakePlayback.setOutput("WASAPI 獨占", ["ReplayGain 已套用", "音量 60%"], "")
            compare(Output.processingText, "ReplayGain 已套用、音量 60%")
        }

        // DSD 直通（假狀態）：滑桿停用，滑鼠移上去說明原因
        function test_disabled_during_dsd() {
            const slider = item("volumeSlider")
            const tip = item("volumeToolTip")
            fakePlayback.setVolume(70)
            fakePlayback.setVolumeAvailable(false)
            verify(!slider.enabled)
            compare(tip.text, "DSD 直通時無法調整音量")
            compare(slider.Accessible.description, "DSD 直通時無法調整音量")
            // 數值還在（恢復可用時就是這個音量）
            compare(item("volumeText").text, "70%")
            const before = volumeCalls().length
            mouseClick(slider, slider.width / 4, slider.height / 2)
            compare(volumeCalls().length, before)
            const area = item("volumeArea")
            mouseMove(area, area.width / 2, area.height / 2)
            tryVerify(() => tip.visible)
            mouseMove(shell, 200, 200)
            tryVerify(() => !tip.visible)

            fakePlayback.setVolumeAvailable(true)
            verify(slider.enabled)
            compare(tip.text, "")
        }

        function test_layout_data() {
            return [{ tag: "960 寬", width: 960 }, { tag: "1600 寬", width: 1600 }]
        }

        // 窄版不擠壞：音量在視窗內、不和輸出按鈕重疊，中段還有地方放曲名
        function test_layout(data) {
            root.width = data.width
            const area = item("volumeArea")
            const slot = item("outputSlot")
            const slider = item("volumeSlider")
            const right = area.mapToItem(shell, area.width, 0).x
            const left = area.mapToItem(shell, 0, 0).x
            const slotRight = slot.mapToItem(shell, slot.width, 0).x
            verify(right <= root.width, "音量超出視窗：" + right)
            verify(slotRight <= left, "輸出按鈕（到 %1）和音量（從 %2）重疊".arg(slotRight).arg(left))
            verify(slider.width >= 48)
            verify(slider.height >= 24)
            const title = item("titleText")
            verify(title.width > 0)
            const titleRight = title.mapToItem(shell, title.width, 0).x
            verify(titleRight <= slot.mapToItem(shell, 0, 0).x)
        }
    }
}
