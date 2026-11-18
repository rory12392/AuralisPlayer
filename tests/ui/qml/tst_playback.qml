import QtQuick
import QtTest
import Auralis.Ui

// 播放列（S-02）與佇列面板（S-13）：在真的主視窗殼裡用鍵盤走一遍播放、暫停、上下首、隨機與重複、
// 佇列的跳轉、移出、排序，再用滑鼠拖曳把手排序、從清單拖曲目進佇列（AC-11-2 的介面部分）。
// Player 背後是假的 PlaybackService（fakePlayback），只確認畫面呼叫了哪個方法。
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

    // 拖放來源：模擬音樂庫清單的一列
    TrackDragArea {
        id: source
        x: 400
        y: 300
        width: 300
        height: 44
        tracks: [{ path: "C:/n/a.flac", subsong: 0 }, { path: "C:/n/b.flac", subsong: 0 }]
        Rectangle {
            anchors.fill: parent
            color: Theme.surfaceRaised
        }
    }

    TestCase {
        name: "Playback"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function lastCall() {
            const calls = fakePlayback.calls
            return calls.length > 0 ? calls[calls.length - 1] : ""
        }

        function drag(from, to) {
            mousePress(root, from.x, from.y)
            for (let i = 1; i <= 10; ++i)
                mouseMove(root, from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10)
            mouseRelease(root, to.x, to.y)
        }

        function init() {
            root.width = 1440
            shell.forceActiveFocus()
        }

        function test_empty_queue() {
            fakePlayback.reset(0, -1)
            verify(!item("playButton").enabled)
            verify(!item("nextButton").enabled)
            compare(item("queueList").count, 0)
            verify(!item("clearQueueButton").enabled)
        }

        function test_transport_keyboard() {
            fakePlayback.reset(4, -1)
            const play = item("playButton")
            verify(play.enabled)
            compare(play.text, "播放")
            play.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "play")
            verify(fakePlayback.isPlaying)
            compare(play.text, "暫停")
            compare(item("titleText").text, "0")
            keyClick(Qt.Key_Return)
            compare(lastCall(), "togglePause")
            verify(!fakePlayback.isPlaying)

            item("nextButton").forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(fakePlayback.current, 1)
            compare(item("titleText").text, "1")
            item("previousButton").forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "previous")
            compare(fakePlayback.current, 0)
        }

        // AC-02-8（T-95）：停止後播放列回到「尚未播放」（曲名、隨機與重複、進度條都不顯示），
        // 停止鈕停用；佇列保留，按播放從目前曲目的位置 0 播並恢復顯示
        function test_stop_button() {
            fakePlayback.reset(3, -1)
            const stop = item("stopButton")
            compare(stop.text, "停止")
            verify(!stop.enabled)
            verify(item("idleText").visible)

            fakePlayback.reset(3, 1)
            item("playButton").forceActiveFocus()
            keyClick(Qt.Key_Space)
            verify(fakePlayback.isPlaying)
            verify(stop.enabled)
            verify(item("nowPlayingInfo").visible)
            verify(item("progressSlider").visible)
            compare(item("titleText").text, "1")
            fakePlayback.tick(42000)

            stop.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "stop")
            verify(!item("nowPlayingInfo").visible)
            verify(!item("titleText").visible)
            verify(!item("progressSlider").visible)
            verify(!item("shuffleButton").visible)
            verify(item("idleText").visible)
            compare(item("idleText").text, "尚未播放")
            verify(!stop.enabled)
            grabImage(shell).save(testTmpDir + "/t-95-stopped.png")
            // 佇列不空：上一首、播放、下一首仍可按
            verify(item("playButton").enabled)
            verify(item("previousButton").enabled)
            verify(item("nextButton").enabled)
            // 佇列保留（清單不含目前曲目那一列）
            compare(fakePlayback.paths.length, 3)
            compare(item("queueList").count, 2)

            item("playButton").forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "play")
            compare(fakePlayback.current, 1)
            compare(Player.positionMs, 0)
            verify(item("nowPlayingInfo").visible)
            compare(item("titleText").text, "1")
            compare(item("positionText").text, "0:00")
            verify(item("stopButton").enabled)

            // 暫停中也可以停止
            item("playButton").forceActiveFocus()
            keyClick(Qt.Key_Space)
            verify(!fakePlayback.isPlaying)
            verify(stop.enabled)
            stop.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "stop")
            verify(!item("nowPlayingInfo").visible)
        }

        // 傳輸鍵由左到右：播放／暫停、停止、上一首、下一首（2026-11-17 Rory 指定）
        function test_transport_order() {
            const names = ["playButton", "stopButton", "previousButton", "nextButton"]
            for (let i = 1; i < names.length; ++i) {
                const left = item(names[i - 1])
                const right = item(names[i])
                verify(left.mapToItem(shell, 0, 0).x < right.mapToItem(shell, 0, 0).x,
                       names[i - 1] + " 要在 " + names[i] + " 左邊")
            }
        }

        // 裝置錯誤造成的停止（AC-02-4）不是「已停止」：照舊顯示曲名與原因
        function test_device_error_keeps_title() {
            fakePlayback.reset(3, 1)
            fakePlayback.setOutput("", [], "輸出裝置已中斷連線")
            verify(Player.hasTrack)
            verify(!Player.stopped)
            verify(item("nowPlayingInfo").visible)
            compare(item("titleText").text, "1")
            verify(!item("idleText").visible)
            verify(!item("stopButton").enabled)
            fakePlayback.setOutput("", [], "")
            verify(Player.stopped)
            verify(!item("nowPlayingInfo").visible)
        }

        function test_shuffle_and_repeat() {
            fakePlayback.resetPaused(3, 0)
            const shuffle = item("shuffleButton")
            shuffle.forceActiveFocus()
            keyClick(Qt.Key_Space)
            verify(Player.shuffle)
            verify(shuffle.checked)
            keyClick(Qt.Key_Return)
            verify(!Player.shuffle)
            verify(!shuffle.checked)

            const repeat = item("repeatButton")
            repeat.forceActiveFocus()
            compare(repeat.text, "重複：關")
            keyClick(Qt.Key_Space)
            compare(repeat.text, "重複：全部")
            verify(repeat.checked)
            keyClick(Qt.Key_Space)
            compare(repeat.text, "重複：單曲")
            compare(repeat.iconName, "repeat-one")
            keyClick(Qt.Key_Space)
            compare(repeat.text, "重複：關")
            verify(!repeat.checked)
        }

        function test_progress_keyboard_seek() {
            fakePlayback.resetPaused(2, 0)
            const progress = item("progressSlider")
            verify(progress.enabled)
            progress.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(lastCall(), "seek 5000")
            // 跳轉後接回播放位置的繫結
            compare(progress.value, 5000)
            compare(item("positionText").text, "0:05")
        }

        function seekCalls() {
            return fakePlayback.calls.filter(call => call.startsWith("seek "))
        }

        // 進度條上 fraction 處（0–1）的滑鼠座標與對應的毫秒數（把手寬度扣掉，和 Slider 的換算相同）
        function pointAt(progress, fraction) {
            const handleWidth = progress.handle.width
            return Qt.point(handleWidth / 2 + (progress.availableWidth - handleWidth) * fraction,
                            progress.height / 2)
        }

        function test_progress_mouse_seek_data() {
            return [
                { tag: "960 寬", width: 960, clickAt: 0.5, dragFrom: 0.2, dragTo: 0.7 },
                { tag: "1600 寬", width: 1600, clickAt: 0.75, dragFrom: 0.6, dragTo: 0.3 }
            ]
        }

        // 修正（播放列進度條跳轉亂跳）：按住與拖曳期間服務仍每 50 毫秒送來播放位置，
        // 進度條要停在使用者指的地方，放開時只跳轉一次、跳到放開的位置，之後不回到舊位置
        function test_progress_mouse_seek(data) {
            root.width = data.width
            fakePlayback.resetPaused(2, 0)
            fakePlayback.tick(10000)
            const progress = item("progressSlider")
            verify(progress.enabled)
            const range = progress.to - progress.from
            const tolerance = range * 0.01

            // 點一下：按下到放開之間有兩次位置更新
            let at = pointAt(progress, data.clickAt)
            mousePress(progress, at.x, at.y)
            fakePlayback.tick(10050)
            fakePlayback.tick(10100)
            mouseRelease(progress, at.x, at.y)
            compare(seekCalls().length, 1)
            const clicked = Number(seekCalls()[0].split(" ")[1])
            fuzzyCompare(clicked, range * data.clickAt, tolerance)
            compare(progress.value, clicked)
            fakePlayback.tick(clicked + 50)
            compare(progress.value, clicked + 50)

            // 拖曳：每移一步就有一次位置更新，把手與時間跟著滑鼠、不被拉回播放位置
            fakePlayback.resetPaused(2, 0)
            fakePlayback.tick(20000)
            const from = pointAt(progress, data.dragFrom)
            const to = pointAt(progress, data.dragTo)
            mousePress(progress, from.x, from.y)
            for (let i = 1; i <= 10; ++i) {
                const x = from.x + (to.x - from.x) * i / 10
                mouseMove(progress, x, from.y)
                fakePlayback.tick(20000 + i * 50)
                const fraction = data.dragFrom + (data.dragTo - data.dragFrom) * i / 10
                fuzzyCompare(progress.value, range * fraction, tolerance)
                compare(item("positionText").text, Player.formatTime(progress.value))
            }
            compare(seekCalls().length, 0)
            mouseRelease(progress, to.x, to.y)
            compare(seekCalls().length, 1)
            const dragged = Number(seekCalls()[0].split(" ")[1])
            fuzzyCompare(dragged, range * data.dragTo, tolerance)
            compare(progress.value, dragged)
        }

        function test_shell_shortcuts() {
            fakePlayback.reset(3, 0)
            shell.forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "play")
            keyClick(Qt.Key_Right, Qt.ControlModifier)
            compare(lastCall(), "next")
            keyClick(Qt.Key_Left, Qt.ControlModifier)
            compare(lastCall(), "previous")
            // 焦點在按鈕上時空白鍵按的是那個按鈕，不是播放／暫停
            item("clearQueueButton").forceActiveFocus()
            keyClick(Qt.Key_Space)
            compare(lastCall(), "clear")
        }

        function test_queue_keyboard() {
            fakePlayback.reset(5, 1)
            const list = item("queueList")
            compare(list.count, 4)
            // 假服務只有目前曲目知道長度（3:20），其他列不知道長度、不算進總長
            compare(item("queueSummary").text, "4 首　3:20")
            list.forceActiveFocus()
            list.currentIndex = 0
            keyClick(Qt.Key_Down)
            compare(list.currentIndex, 1)
            // Ctrl+↓：第 1 列（佇列第 2 項）往下移一格
            keyClick(Qt.Key_Down, Qt.ControlModifier)
            compare(lastCall(), "move 2 3")
            compare(list.currentIndex, 2)
            compare(fakePlayback.paths[3], "C:/t/2.flac")
            keyClick(Qt.Key_Up, Qt.ControlModifier)
            compare(lastCall(), "move 3 2")
            compare(list.currentIndex, 1)
            // 「正在播放」那列不能移
            list.currentIndex = 0
            keyClick(Qt.Key_Down, Qt.ControlModifier)
            compare(lastCall(), "move 3 2")
            list.currentIndex = 1
            keyClick(Qt.Key_Delete)
            compare(lastCall(), "remove 2")
            compare(list.count, 3)
            compare(list.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(lastCall(), "playAt 2")
        }

        function test_queue_drag_handle_reorder() {
            fakePlayback.reset(5, 0)
            const list = item("queueList")
            compare(list.count, 5)
            const handle = findChild(list.itemAtIndex(3), "dragHandle")
            const target = list.itemAtIndex(1)
            const from = handle.mapToItem(root, handle.width / 2, handle.height / 2)
            const to = target.mapToItem(root, target.width - 10, target.height / 2)
            drag(from, to)
            compare(lastCall(), "move 3 1")
            compare(fakePlayback.paths[1], "C:/t/3.flac")
        }

        function test_drop_tracks_into_queue() {
            fakePlayback.reset(4, 0)
            const list = item("queueList")
            const from = source.mapToItem(root, 20, 22)

            // 放在第 1 列的上半部＝正在播放的下一首（接著播放）
            const row = list.itemAtIndex(1)
            drag(from, row.mapToItem(root, row.width / 2, 4))
            tryCompare(fakePlayback, "paths", ["C:/t/0.flac", "C:/n/a.flac", "C:/n/b.flac",
                                                "C:/t/1.flac", "C:/t/2.flac", "C:/t/3.flac"])
            compare(lastCall(), "enqueueNext 2")

            // 放在清單下方的空白＝加到最後
            fakePlayback.reset(2, 0)
            drag(from, list.mapToItem(root, list.width / 2, list.height - 20))
            tryCompare(fakePlayback, "paths", ["C:/t/0.flac", "C:/t/1.flac", "C:/n/a.flac", "C:/n/b.flac"])
            compare(lastCall(), "enqueueLast 2")
        }

        function test_double_click_plays_row() {
            fakePlayback.reset(3, 0)
            const row = item("queueList").itemAtIndex(2)
            mouseDoubleClickSequence(row, 60, row.height / 2)
            compare(lastCall(), "playAt 2")
        }
    }
}
