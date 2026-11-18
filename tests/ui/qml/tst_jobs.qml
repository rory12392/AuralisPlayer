import QtQuick
import QtTest
import Auralis.Ui

// 狀態列的背景工作（T-93，介面規範 §6「背景工作」）：執行不到約 300 ms 的工作不出現；長的工作顯示
// ProgressLine＋名稱＋「取消」，結束或取消後消失；好幾個時顯示最早的加「＋n 個」；掃描（「音樂庫」）
// 不在這裡重複顯示；960 寬、面板展開時不超出狀態列。工作由 jobsProbe 排進核心的 JobRunner。
// 最後在 1600×900 Void 截一張（testTmpDir），收尾時搬進 docs/evidence/screenshots/。
Item {
    id: root
    width: 1600
    height: 900

    AppShell {
        id: shell
        width: root.width
        height: root.height
        playbarSource: "qrc:/qt/qml/Auralis/Ui/shell/Playbar.qml"
    }

    SignalSpy {
        id: jobsChanged
        target: Jobs
        signalName: "changed"
    }

    TestCase {
        name: "Jobs"
        when: windowShown

        function item(name) {
            const found = findChild(shell, name)
            verify(found !== null, name)
            return found
        }

        function init() {
            root.width = 1600
            root.height = 900
            ShellState.panelOpen = false
            shell.forceActiveFocus()
            jobsProbe.finishAll()
            tryVerify(() => !Jobs.active, 3000, "上一個測試的工作還在")
            jobsChanged.clear()
        }

        function cleanup() {
            jobsProbe.finishAll()
        }

        function test_short_job_hidden() {
            const id = jobsProbe.start("查詢線上歌詞", false)
            wait(50)
            jobsProbe.finish(id)
            // 等過顯示的門檻，期間狀態列一次都沒變
            wait(600)
            compare(jobsChanged.count, 0)
            verify(!item("jobStatus").visible)
        }

        function test_long_job_shows_name_and_cancel() {
            const id = jobsProbe.start("查詢線上歌詞", false)
            verify(!item("jobStatus").visible, "剛排入時還不顯示")
            tryVerify(() => item("jobStatus").visible, 2000)
            compare(item("jobName").text, "查詢線上歌詞")
            verify(item("jobCancel").visible)
            compare(item("jobCancel").Accessible.name, "取消查詢線上歌詞")
            verify(!item("jobMore").visible)
            verify(Jobs.indeterminate, "沒有回報進度時是不確定")
            jobsProbe.finish(id)
            tryVerify(() => !item("jobStatus").visible, 2000, "結束後消失")
        }

        function test_progress_shown() {
            const id = jobsProbe.start("計算 ReplayGain", true)
            tryVerify(() => item("jobStatus").visible, 2000)
            verify(!Jobs.indeterminate)
            compare(Jobs.progress, 0.4)
            jobsProbe.finish(id)
            tryVerify(() => !item("jobStatus").visible, 2000)
        }

        function test_cancel_by_click() {
            const id = jobsProbe.start("計算 ReplayGain", false)
            tryVerify(() => item("jobStatus").visible, 2000)
            waitForRendering(shell)
            mouseClick(item("jobCancel"))
            verify(!item("jobStatus").visible, "按取消後馬上消失")
            tryVerify(() => jobsProbe.sawCancel(id), 2000, "工作收到取消")
        }

        // 鍵盤：Tab 能走到狀態列的「取消」，空白鍵取消
        function test_cancel_by_keyboard() {
            const id = jobsProbe.start("查詢線上歌詞", false)
            tryVerify(() => item("jobStatus").visible, 2000)
            const cancel = item("jobCancel")
            item("pageToolbar").forceActiveFocus()
            let presses = 0
            while (!cancel.activeFocus && presses < 80) {
                keyClick(Qt.Key_Tab)
                ++presses
            }
            verify(cancel.activeFocus, "Tab 走不到取消")
            keyClick(Qt.Key_Space)
            verify(!item("jobStatus").visible)
            tryVerify(() => jobsProbe.sawCancel(id), 2000)
        }

        function test_several_jobs() {
            const first = jobsProbe.start("查詢線上歌詞", false)
            const second = jobsProbe.start("計算 ReplayGain", false)
            const third = jobsProbe.start("查詢線上歌詞", false)
            tryVerify(() => Jobs.moreCount === 2, 2000)
            compare(item("jobName").text, "查詢線上歌詞")
            verify(item("jobMore").visible)
            compare(item("jobMore").text, "＋2 個")
            jobsProbe.finish(first)
            tryVerify(() => item("jobName").text === "計算 ReplayGain", 2000)
            compare(item("jobMore").text, "＋1 個")
            jobsProbe.finish(third)
            tryVerify(() => !item("jobMore").visible, 2000)
            verify(item("jobStatus").visible)
            jobsProbe.finish(second)
            tryVerify(() => !item("jobStatus").visible, 2000)
        }

        // 掃描有自己的一塊（scanStatus），不重複顯示
        function test_scan_job_not_repeated() {
            const id = jobsProbe.start("音樂庫", false)
            wait(600)
            verify(!item("jobStatus").visible)
            compare(jobsChanged.count, 0)
            jobsProbe.finish(id)
        }

        // 960 寬、面板展開、名稱很長：整塊仍在狀態列內，名稱省略
        function test_narrow_window() {
            root.width = 960
            root.height = 640
            ShellState.panelOpen = true
            const id = jobsProbe.start("查詢線上歌詞：一個非常非常長的工作名稱，用來確認狀態列不會被撐破", false)
            const other = jobsProbe.start("計算 ReplayGain", false)
            tryVerify(() => item("jobStatus").visible, 2000)
            waitForRendering(shell)
            const right = item("statusRight")
            const bar = right.parent
            const left = right.mapToItem(bar, 0, 0).x
            verify(left >= 0, "狀態列右邊那排的左緣 %1 超出狀態列".arg(left))
            verify(left + right.width <= bar.width, "右緣超出狀態列")
            const name = item("jobName")
            verify(name.width < name.implicitWidth, "名稱應該被省略")
            jobsProbe.finish(id)
            jobsProbe.finish(other)
        }

        function test_screenshot() {
            const original = ThemeController.mode
            ThemeController.overrideMode("void")
            const id = jobsProbe.start("查詢線上歌詞", true)
            const other = jobsProbe.start("計算 ReplayGain", false)
            tryVerify(() => item("jobStatus").visible && Jobs.moreCount === 1, 2000)
            waitForRendering(shell)
            // save 失敗時丟例外，測試就失敗
            grabImage(shell).save(testTmpDir + "/t-93-status-jobs-1600x900.png")
            ThemeController.overrideMode(original)
            jobsProbe.finish(id)
            jobsProbe.finish(other)
        }
    }
}
