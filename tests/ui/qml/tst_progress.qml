import QtQuick
import QtTest
import Auralis.Ui

// 播放列進度條的落點（S-02）：在軌道的某個像素按下、放開，跳轉的毫秒數要等於「滑鼠在軌道上的比例 × 長度」，
// 把手中心要停在滑鼠的位置（誤差 1 像素以內）。Player 背後是假的 PlaybackService（長度 200 秒）。
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
        name: "Progress"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function seekCalls() {
            return fakePlayback.calls.filter(call => call.startsWith("seek "))
        }

        // 把手中心能到的範圍：左右各留半個把手寬
        function travel(progress) {
            return progress.availableWidth - progress.handle.width
        }

        // 滑鼠在 x（進度條自己的座標，像素）時該有的毫秒數
        function valueAt(progress, x) {
            const fraction = (x - progress.leftPadding - progress.handle.width / 2) / travel(progress)
            return (progress.to - progress.from) * Math.max(0, Math.min(1, fraction))
        }

        function handleCenter(progress) {
            return progress.handle.x + progress.handle.width / 2
        }

        function init() {
            root.width = 1440
            shell.forceActiveFocus()
        }

        function test_pointer_lands_data() {
            // 比例刻意不落在 5 秒的倍數上
            return [
                { tag: "960 寬", width: 960, fractions: [0.031, 0.137, 0.333, 0.5, 0.618, 0.813, 0.969] },
                { tag: "1600 寬", width: 1600, fractions: [0.031, 0.137, 0.333, 0.5, 0.618, 0.813, 0.969] }
            ]
        }

        // 點一下：跳轉目標與把手都在滑鼠的位置，之後播放位置繼續送來也不位移
        function test_pointer_lands(data) {
            root.width = data.width
            fakePlayback.resetPaused(2, 0)
            const progress = item("progressSlider")
            verify(progress.enabled)
            const msPerPixel = (progress.to - progress.from) / travel(progress)
            for (const fraction of data.fractions) {
                fakePlayback.resetPaused(2, 0)
                fakePlayback.tick(10000)
                const x = Math.round(progress.handle.width / 2 + travel(progress) * fraction)
                const y = progress.height / 2
                mousePress(progress, x, y)
                fakePlayback.tick(10050)
                verify(Math.abs(handleCenter(progress) - x) <= 1,
                       "按住時把手中心 %1，滑鼠 %2".arg(handleCenter(progress)).arg(x))
                mouseRelease(progress, x, y)
                compare(seekCalls().length, 1)
                const sought = Number(seekCalls()[0].split(" ")[1])
                const offPixels = (sought - valueAt(progress, x)) / msPerPixel
                verify(Math.abs(offPixels) <= 1,
                       "滑鼠 x=%1 應該跳到 %2 毫秒，實際 %3（差 %4 像素）"
                           .arg(x).arg(Math.round(valueAt(progress, x))).arg(sought).arg(offPixels.toFixed(2)))
                verify(Math.abs(handleCenter(progress) - x) <= 1,
                       "放開後把手中心 %1，滑鼠 %2".arg(handleCenter(progress)).arg(x))
                compare(item("positionText").text, Player.formatTime(sought))
            }
        }

        // 左右鍵仍然一次 5 秒，從目前的播放位置算（不是取到 5 秒的倍數）
        function test_keys_step_from_position() {
            fakePlayback.resetPaused(2, 0)
            fakePlayback.tick(12345)
            const progress = item("progressSlider")
            progress.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(seekCalls().length, 1)
            compare(seekCalls()[0], "seek 17345")
            compare(progress.value, 17345)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(seekCalls()[2], "seek 7345")
            // 接回播放位置
            fakePlayback.tick(7400)
            compare(progress.value, 7400)
        }

        // 拖曳：每一步把手都跟著滑鼠，放開時跳到放開的位置
        function test_drag_lands(data) {
            root.width = data.width
            fakePlayback.resetPaused(2, 0)
            fakePlayback.tick(20000)
            const progress = item("progressSlider")
            const msPerPixel = (progress.to - progress.from) / travel(progress)
            const y = progress.height / 2
            const from = Math.round(progress.handle.width / 2 + travel(progress) * 0.21)
            const to = Math.round(progress.handle.width / 2 + travel(progress) * 0.683)
            mousePress(progress, from, y)
            for (let i = 1; i <= 16; ++i) {
                const x = Math.round(from + (to - from) * i / 16)
                mouseMove(progress, x, y)
                fakePlayback.tick(20000 + i * 50)
                verify(Math.abs(handleCenter(progress) - x) <= 1,
                       "拖曳中把手中心 %1，滑鼠 %2".arg(handleCenter(progress)).arg(x))
                verify(Math.abs(progress.value - valueAt(progress, x)) <= msPerPixel,
                       "拖曳中的值 %1，滑鼠位置是 %2".arg(progress.value).arg(valueAt(progress, x)))
            }
            mouseRelease(progress, to, y)
            compare(seekCalls().length, 1)
            const sought = Number(seekCalls()[0].split(" ")[1])
            verify(Math.abs(sought - valueAt(progress, to)) <= msPerPixel,
                   "放開在 x=%1 應該跳到 %2 毫秒，實際 %3".arg(to).arg(Math.round(valueAt(progress, to))).arg(sought))
            verify(Math.abs(handleCenter(progress) - to) <= 1)
        }

        function test_drag_lands_data() {
            return [{ tag: "960 寬", width: 960 }, { tag: "1600 寬", width: 1600 }]
        }
    }
}
