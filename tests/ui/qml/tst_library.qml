import QtQuick
import QtTest
import Auralis.Ui

// 歡迎畫面（S-01）、設定頁（S-16）與掃描報告（S-17）：用鍵盤走一遍加入資料夾、看掃描報告、
// 切換主題、移除資料夾。Library 背後是真的 LibraryManager（測試暫存的資料庫），資料夾由
// libraryProbe 產生；Dialogs 的請求由測試回覆（不開系統的資料夾對話框）。
Item {
    id: root
    width: 1440
    height: 900

    property var pending: null

    Connections {
        target: Dialogs
        function onRequested(id, kind, options) {
            root.pending = { id: id, kind: kind, options: options }
        }
    }

    Loader {
        id: pageLoader
        anchors.fill: parent
        focus: true
    }

    TestCase {
        name: "Library"
        when: windowShown

        function item(name) {
            const found = findChild(pageLoader.item, name)
            verify(found !== null, name)
            return found
        }

        function contains(ancestor, child) {
            for (let it = child; it; it = it.parent) {
                if (it === ancestor)
                    return true
            }
            return false
        }

        // 按 Tab 直到焦點進到 target 裡
        function tabTo(target) {
            for (let i = 0; i < 20 && !contains(target, root.Window.activeFocusItem); ++i)
                keyClick(Qt.Key_Tab)
            verify(contains(target, root.Window.activeFocusItem), "Tab 到得了 " + target.objectName)
        }

        function load(name) {
            pageLoader.source = ""
            pageLoader.source = "qrc:/qt/qml/Auralis/Ui/shell/" + name + ".qml"
            tryVerify(() => pageLoader.status === Loader.Ready)
            pageLoader.forceActiveFocus()
        }

        function init() {
            // 移除上一個測試加入的資料夾
            for (const folder of Library.folders) {
                Library.confirmRemove(folder.path)
                Dialogs.respond(root.pending.id, true)
            }
            tryVerify(() => !Library.hasFolders && !Library.scanning, 10000)
            root.pending = null
        }

        function cleanupTestCase() {
            init()
            pageLoader.source = ""
        }

        function test_welcome_keyboard() {
            const path = libraryProbe.makeFolder("welcome", 3, true)
            load("WelcomePage")
            const title = item("welcomeTitle")
            compare(title.text, "加入你的音樂資料夾")

            // 鍵盤：Tab 到「加入音樂資料夾」、Enter 開資料夾選擇
            tabTo(item("welcomeAdd"))
            keyClick(Qt.Key_Return)
            verify(root.pending !== null)
            compare(root.pending.kind, "folder")
            Dialogs.respond(root.pending.id, [path])

            // AC-08-1：加入後在背景掃描，同一個位置換成掃描進度
            compare(title.text, "正在掃描 " + path)
            verify(item("welcomeProgress").visible)
            verify(item("welcomeContinue").visible)

            // 完成：加入幾首、讀不到的檔案（AC-08-5）
            tryCompare(title, "text", "已加入 3 首曲目", 10000)
            compare(item("welcomeFailed").text, "1 個檔案讀不到")
            const reportButton = item("welcomeReport")
            tabTo(reportButton)
            keyClick(Qt.Key_Return)
            const dialog = item("scanReportDialog")
            tryVerify(() => dialog.opened)
            // 對話框的高度容得下標題、內容與頁尾（共用 Dialog 原本高度是 0）
            verify(dialog.height >= dialog.implicitHeaderHeight + dialog.implicitContentHeight
                   + dialog.implicitFooterHeight)
            compare(dialog.report.failed, 1)
            compare(dialog.failures.length, 1)
            compare(dialog.failures[0].reason, "檔案損毀")
            verify(dialog.confirmVisible) // 有讀不到的檔案才有「複製清單」
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
        }

        function test_settings_keyboard() {
            const path = libraryProbe.makeFolder("settings", 2, false)
            load("SettingsPage")

            // 加入資料夾：Tab 到按鈕、Enter
            tabTo(item("settingsAddFolder"))
            keyClick(Qt.Key_Return)
            compare(root.pending.kind, "folder")
            Dialogs.respond(root.pending.id, [path])
            tryVerify(() => !Library.scanning && Library.folders.length === 1
                      && Library.folders[0].trackCount === 2, 10000)
            verify(item("folderRow0").visible)

            // 外觀：主題的 SegmentedControl 用方向鍵切換（AC-19-2，寫進 ui/theme）
            const original = ThemeController.mode
            const control = item("themeControl")
            tabTo(control)
            const modes = ["system", "void", "paper"]
            const before = modes.indexOf(ThemeController.mode)
            keyClick(before < 2 ? Qt.Key_Right : Qt.Key_Left)
            compare(ThemeController.mode, modes[before < 2 ? before + 1 : before - 1])
            compare(control.currentIndex, modes.indexOf(ThemeController.mode))
            ThemeController.mode = original

            // 掃描報告：沒有問題時寫「所有檔案都讀取成功。」，只有「關閉」
            tabTo(item("settingsReport"))
            keyClick(Qt.Key_Return)
            const dialog = item("scanReportDialog")
            tryVerify(() => dialog.opened)
            compare(dialog.failures.length, 0)
            verify(!dialog.confirmVisible)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)

            // 移除：先確認，取消時保留、確認後移除
            tabTo(item("folderRemove0"))
            keyClick(Qt.Key_Return)
            compare(root.pending.kind, "confirm")
            compare(root.pending.options.title, "移除音樂資料夾 " + path)
            compare(root.pending.options.confirmText, "移除資料夾")
            Dialogs.respond(root.pending.id, false)
            compare(Library.folders.length, 1)
            tabTo(item("folderRemove0"))
            keyClick(Qt.Key_Return)
            Dialogs.respond(root.pending.id, true)
            tryVerify(() => !Library.hasFolders, 10000)
        }

        function test_settings_module_sections() {
            // 模組用 addSettingsSection 登記的區塊（quick_test_main 登記了一個正常、一個載不到的）：
            // 正常的照內容高度出現；載不到的只有那一塊顯示錯誤區塊，其他分區照常
            load("SettingsPage")
            const ok = item("settingsSection-test/ok")
            tryVerify(() => ok.item !== null)
            compare(ok.height, ok.item.implicitHeight)
            verify(findChild(ok.item, "fixtureCheckbox") !== null)
            const broken = item("settingsSection-test/broken")
            // PageArea 的錯誤區塊（有 retry 訊號的那個子項目）
            const error = Array.from(broken.children).find(c => typeof c.retry === "function")
            verify(error !== undefined)
            tryVerify(() => error.visible)
            verify(item("themeControl").visible)
            verify(item("settingsAddFolder").visible)
        }
    }
}
