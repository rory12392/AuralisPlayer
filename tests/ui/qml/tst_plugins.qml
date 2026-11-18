import QtQuick
import QtTest
import Auralis.Ui

// 設定頁的「外掛」分區（S-16，AC-03-0，T-104）：ASIO 外掛的四種狀態、匯入成功與失敗、移除（先確認）、
// 停用、下載頁只交給瀏覽器、商標標示。Plugins 背後是假的外掛管理（pluginsProbe）；
// Dialogs 的確認請求由測試回覆。系統的開檔對話框不開，選檔的結果直接交給 Plugins.importFile。
Item {
    id: root
    width: 960
    height: 900

    property var pending: null

    Connections {
        target: Dialogs
        function onRequested(id, kind, options) {
            root.pending = { id: id, kind: kind, options: options }
        }
    }

    SettingsPlugins {
        id: plugins
        width: 720
    }

    TestCase {
        name: "Plugins"
        when: windowShown

        // 清單改變時 Repeater 重建列，舊列晚一點才刪：先讓延後的刪除跑完再找
        function item(name) {
            wait(0)
            const found = findChild(plugins, name)
            verify(found !== null, name)
            return found
        }

        function asioRow() {
            const row = Plugins.rows.find(r => r.asio)
            verify(row !== undefined, "ASIO 那一列")
            return row
        }

        function asio(state, extra) {
            const row = { id: "AuralisAsio", name: "ASIO 輸出", type: "output", version: "0.1.0",
                          state: state, pending: "none" }
            pluginsProbe.setPlugins([Object.assign(row, extra || {})])
        }

        function init() {
            pluginsProbe.reset()
            root.pending = null
        }

        function test_asio_states() {
            // AC-03-0：未安裝 → 佔位列（沒有開關與移除），狀態句「未安裝」
            compare(asioRow().stateText, "未安裝")
            compare(item("pluginsAsioStatus").text, "ASIO 外掛：未安裝")
            verify(!item("pluginEnabled-AuralisAsio").visible)
            verify(!item("pluginRemove-AuralisAsio").visible)
            // 已安裝（版本）
            asio("enabled")
            compare(item("pluginsAsioStatus").text, "ASIO 外掛：已安裝（0.1.0）")
            compare(item("pluginState-AuralisAsio").text, "已啟用")
            compare(item("pluginState-AuralisAsio").variant, "neutral")
            verify(item("pluginEnabled-AuralisAsio").visible)
            verify(item("pluginEnabled-AuralisAsio").checked)
            verify(item("pluginRemove-AuralisAsio").visible)
            // 版本不符
            asio("versionMismatch", { reason: "外掛是給主程式 0.2.0–0.2.0 的" })
            compare(item("pluginsAsioStatus").text,
                    "ASIO 外掛：版本不符，請下載和目前版本相同的外掛")
            compare(item("pluginState-AuralisAsio").variant, "signal")
            verify(!item("pluginEnabled-AuralisAsio").visible)
            // 檔案不符或損毀（原因）
            asio("fileMismatch", { reason: "不是這個版本的官方外掛（檔案不符）" })
            compare(item("pluginsAsioStatus").text,
                    "ASIO 外掛：檔案不符或損毀（不是這個版本的官方外掛（檔案不符））")
            asio("invalid", { reason: "plugin.json 缺少 dll 欄位" })
            compare(item("pluginsAsioStatus").text, "ASIO 外掛：檔案不符或損毀（plugin.json 缺少 dll 欄位）")
            compare(item("pluginState-AuralisAsio").text, "plugin.json 缺少 dll 欄位")
        }

        function test_trademark() {
            // 第一次出現的 ASIO 加 ®；ASIO 區有商標聲明；外掛沒有標誌檔時只顯示文字、標誌位置不佔空間
            verify(asioRow().name.indexOf("ASIO®") === 0)
            asio("enabled")
            verify(asioRow().name.indexOf("ASIO®") === 0)
            compare(item("asioTrademarkText").text,
                    "ASIO is a registered trademark of Steinberg Media Technologies GmbH.")
            verify(item("asioTrademark").visible)
            verify(!item("asioLogo").visible)
            compare(item("asioLogo").width, 0)
        }

        function test_logo_follows_theme() {
            // T-105：標誌從已載入的 ASIO 外掛資料夾讀，深色主題用白色透明版、淺色用黑白版
            const original = ThemeController.mode
            const dark = pluginsProbe.repoLogo("dark")
            const light = pluginsProbe.repoLogo("light")
            asio("enabled", { builtin: true, logoDark: dark, logoLight: light })
            ThemeController.overrideMode("void")
            verify(Theme.dark)
            compare(item("asioLogo").source.toString(), "file:///" + dark)
            tryCompare(item("asioLogo"), "status", Image.Ready)
            verify(item("asioLogo").visible)
            compare(item("asioLogo").height, Theme.space12)
            verify(item("asioLogo").width > 0)
            ThemeController.overrideMode("paper")
            verify(!Theme.dark)
            compare(item("asioLogo").source.toString(), "file:///" + light)
            tryCompare(item("asioLogo"), "status", Image.Ready)
            verify(item("asioLogo").visible)
            // 外掛停用（這次沒載入）時不顯示標誌，只有文字
            asio("disabled", { builtin: true, logoDark: dark, logoLight: light })
            verify(!item("asioLogo").visible)
            verify(item("asioTrademarkText").visible)
            ThemeController.overrideMode(original)
        }

        function test_builtin() {
            // T-105：內建（隨主程式安裝）的外掛顯示「內建」，可以停用、沒有移除
            asio("enabled", { builtin: true })
            verify(item("pluginBuiltin-AuralisAsio").visible)
            verify(item("pluginEnabled-AuralisAsio").visible)
            verify(!item("pluginRemove-AuralisAsio").visible)
            mouseClick(item("pluginEnabled-AuralisAsio"))
            compare(pluginsProbe.calls, ["enable:AuralisAsio:false"])
            compare(item("pluginPending-AuralisAsio").text, "重新啟動後停用")
            // 使用者匯入的（不是內建）照舊有移除、沒有「內建」
            asio("enabled")
            verify(!item("pluginBuiltin-AuralisAsio").visible)
            verify(item("pluginRemove-AuralisAsio").visible)
        }

        function test_import_success() {
            // 選 DLL 時用它旁邊的 plugin.json；成功後「重新啟動後生效」，清單多一列待安裝
            const dir = pluginsProbe.makePluginFolder("ok", true)
            Plugins.importFile("file:///" + dir + "/AuralisAsio.dll")
            compare(pluginsProbe.calls, ["import:" + dir + "/plugin.json"])
            compare(Plugins.message, "重新啟動後生效。")
            verify(!Plugins.messageIsError)
            verify(Plugins.restartNeeded)
            verify(item("pluginsMessage").visible)
            compare(item("pluginsAsioStatus").text, "ASIO 外掛：已匯入（0.1.0），重新啟動後安裝")
            // 起始資料夾是「下載」，不是音樂資料夾
            verify(Plugins.dialogFolder.toString().length > 0)
            verify(Plugins.dialogFolder.toString().toLowerCase().indexOf("download") >= 0)
        }

        function test_import_failure() {
            // 外掛管理拒絕（不是官方外掛）：原因用 signal 顯示，清單不變
            const dir = pluginsProbe.makePluginFolder("bad", true)
            pluginsProbe.failNext("不是這個版本的官方外掛（檔案不符）")
            Plugins.importFile("file:///" + dir + "/plugin.json")
            compare(Plugins.message, "不是這個版本的官方外掛（檔案不符）")
            verify(Plugins.messageIsError)
            compare(item("pluginsMessage").color, Theme.signal)
            compare(asioRow().stateText, "未安裝")
            verify(!Plugins.restartNeeded)
            // 沒有 plugin.json 的資料夾：不呼叫外掛管理，說明要選什麼
            const empty = pluginsProbe.makePluginFolder("nomanifest", false)
            Plugins.importFile("file:///" + empty + "/AuralisAsio.dll")
            compare(pluginsProbe.calls.length, 1)
            verify(Plugins.message.indexOf("plugin.json") >= 0)
            verify(Plugins.messageIsError)
        }

        function test_remove_and_disable() {
            asio("enabled")
            // 移除：先確認；取消時不動，確認後重新啟動時移除
            mouseClick(item("pluginRemove-AuralisAsio"))
            compare(root.pending.kind, "confirm")
            compare(root.pending.options.title, "移除外掛 ASIO 輸出")
            compare(root.pending.options.confirmText, "移除外掛")
            Dialogs.respond(root.pending.id, false)
            compare(pluginsProbe.calls, [])
            mouseClick(item("pluginRemove-AuralisAsio"))
            Dialogs.respond(root.pending.id, true)
            compare(pluginsProbe.calls, ["remove:AuralisAsio"])
            compare(Plugins.message, "重新啟動後生效。")
            compare(item("pluginPending-AuralisAsio").text, "重新啟動後移除")
            verify(!item("pluginRemove-AuralisAsio").visible)
            verify(!item("pluginEnabled-AuralisAsio").visible)

            // 停用：勾掉「啟用」，重新啟動後停用；開關顯示新的值
            asio("enabled")
            mouseClick(item("pluginEnabled-AuralisAsio"))
            compare(pluginsProbe.calls.slice(-1)[0], "enable:AuralisAsio:false")
            compare(item("pluginPending-AuralisAsio").text, "重新啟動後停用")
            verify(!item("pluginEnabled-AuralisAsio").checked)
            verify(Plugins.restartNeeded)
        }

        function test_release_page() {
            // 下載頁只交給瀏覽器開，程式不下載
            mouseClick(item("pluginsRelease"))
            compare(pluginsProbe.openedUrls, ["https://github.com/rory12392/AuralisPlayer/releases"])
            compare(pluginsProbe.calls, [])
        }
    }
}
