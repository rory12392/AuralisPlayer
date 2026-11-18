import QtQuick
import QtTest
import Auralis.Ui

// 曲目右鍵選單（TrackMenu，T-26）：核心項目接到佇列、模組區塊（UiRegistry.trackMenuSections）依 order
// 出現並收到 payloadJson、載不到的區塊只讓那一段不出現、只帶查詢條件時核心項目照樣能用（T-62）、「開啟檔案位置」
// 只在一首時出現、開選單不碰檔案系統、按下時檔案不在就顯示錯誤通知。登記的區塊見 quick_test_main.cpp：
// 子選單（order 10）、載不到的（15）、單一項目（20）、一組兩個項目（30）。Player 背後是假的
// PlaybackService（fakePlayback）；FileLocation 的開啟檔案總管與存在檢查換成 trackMenuProbe 記錄。
Item {
    id: root
    width: 800
    height: 600

    Button {
        id: trigger
        text: "曲目"
    }

    TrackMenu {
        id: menu
    }

    function payload(count) {
        const tracks = []
        for (let i = 0; i < count; ++i)
            tracks.push({ path: "C:/t/" + i + ".flac", subsong: 0 })
        return TrackDragCodec.encode(tracks, "", count)
    }

    TestCase {
        name: "TrackMenu"
        when: windowShown

        function init() {
            fakePlayback.reset(0, -1)
            trackMenuProbe.clear()
            trigger.forceActiveFocus(Qt.TabFocusReason)
        }

        function cleanup() {
            menu.dismiss()
            tryCompare(menu, "visible", false)
        }

        function openWithKeyboard(json, count) {
            menu.popupFrom(json, count, trigger)
            tryCompare(menu, "opened", true)
        }

        function texts() {
            const result = []
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i)
                if (item.visible)
                    result.push(item.text)
            }
            return result
        }

        function test_core_items_and_sections_in_order() {
            openWithKeyboard(root.payload(2), 2)
            // 載不到的區塊（order 15）不出現，其他照 order；兩首時沒有「開啟檔案位置」
            compare(texts(), ["播放", "接著播放", "加到佇列最後", "加入播放清單",
                              "計算 ReplayGain", "前往專輯", "前往藝人"])
            compare(menu.itemAt(0).shortcut, "Enter")
            compare(menu.itemAt(1).shortcut, "Shift+Enter")
            compare(menu.itemAt(2).shortcut, "Ctrl+Enter")
            verify(menu.itemAt(3).subMenu !== null)
        }

        function test_sections_receive_payload() {
            const json = root.payload(3)
            openWithKeyboard(json, 3)
            const single = menu.itemAt(4)
            compare(single.objectName, "fixtureSingle")
            compare(single.payloadJson, json)
            compare(single.trackCount, 3)
            compare(menu.itemAt(3).subMenu.payloadJson, json)
            // 再開一次換了曲目：區塊重建，收到新的內容
            menu.dismiss()
            tryCompare(menu, "visible", false)
            const other = root.payload(1)
            openWithKeyboard(other, 1)
            tryCompare(menu.itemAt(4), "payloadJson", other)
            compare(texts().filter(text => text === "計算 ReplayGain").length, 1)
        }

        function test_keyboard_play() {
            openWithKeyboard(root.payload(2), 2)
            // 用鍵盤開時第一項已經是目前項目
            compare(menu.currentIndex, 0)
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false)
            compare(fakePlayback.calls, ["playFrom 0"])
            compare(fakePlayback.paths, ["C:/t/0.flac", "C:/t/1.flac"])
            tryCompare(root.Window, "activeFocusItem", trigger)
        }

        function test_keyboard_enqueue_next_and_last() {
            fakePlayback.reset(3, 0)
            openWithKeyboard(root.payload(2), 2)
            keyClick(Qt.Key_Down)
            compare(menu.currentIndex, 1)
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false)
            openWithKeyboard(root.payload(1), 1)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false)
            compare(fakePlayback.calls, ["enqueueNext 2", "enqueueLast 1"])
        }

        function test_escape_closes() {
            openWithKeyboard(root.payload(1), 1)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "visible", false)
            compare(fakePlayback.calls, [])
            tryCompare(root.Window, "activeFocusItem", trigger)
        }

        // AC-11-4（T-62）：只帶查詢條件時前三項照樣能用，曲目由 Player 在背景取（quick_test_main 的
        // 假音樂庫：「q:<n>」取到 n 首）
        function test_query_only_core_items_enabled() {
            const json = TrackDragCodec.encode([], "q:6000", 6000)
            openWithKeyboard(json, 6000)
            verify(menu.itemAt(0).enabled)
            verify(menu.itemAt(1).enabled)
            verify(menu.itemAt(2).enabled)
            // 模組區塊照樣出現、收到同一份內容；鍵盤從第一項開始
            compare(menu.itemAt(4).payloadJson, json)
            compare(menu.itemAt(4).trackCount, 6000)
            compare(menu.currentIndex, 0)
            keyClick(Qt.Key_Return)
            tryCompare(menu, "visible", false)
            tryCompare(fakePlayback, "calls", ["playFrom 0"])
            compare(fakePlayback.paths.length, 6000)
            compare(fakePlayback.paths[5999], "C:/q/5999.flac")
        }

        function test_query_only_enqueue_last() {
            fakePlayback.reset(2, 0)
            openWithKeyboard(TrackDragCodec.encode([], "q:6000", 6000), 6000)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            tryCompare(fakePlayback, "calls", ["enqueueLast 6000"])
        }

        function test_show_in_folder_for_one_existing_file() {
            const existing = trackMenuProbe.existingFile
            const json = TrackDragCodec.encode([{ path: existing, subsong: 0 }], "", 1)
            openWithKeyboard(json, 1)
            const last = menu.itemAt(menu.count - 1)
            compare(last.text, "開啟檔案位置")
            verify(last.visible && last.enabled)
            // 開選單時不檢查檔案（離線磁碟、睡眠中的硬碟會讓介面卡住），按下才在背景檢查
            compare(trackMenuProbe.existsCalls(), 0)
            last.triggered()
            tryCompare(menu, "visible", false)
            tryVerify(() => trackMenuProbe.revealed.length === 1)
            compare(trackMenuProbe.revealed, [existing])
            compare(fakePlayback.calls, [])
        }

        function test_show_in_folder_missing_file_notifies() {
            // 只有一首、有路徑就出現；檔案不在時按下只顯示錯誤通知、不開檔案總管
            openWithKeyboard(root.payload(1), 1)
            const last = menu.itemAt(menu.count - 1)
            verify(last.visible && last.enabled)
            compare(trackMenuProbe.existsCalls(), 0)
            last.triggered()
            tryCompare(menu, "visible", false)
            tryVerify(() => Notifier.current.text === "找不到檔案：0.flac")
            compare(Notifier.current.kind, "error")
            verify(trackMenuProbe.existsCalls() > 0)
            compare(trackMenuProbe.revealed, [])
        }

        function test_show_in_folder_hidden_for_several_tracks() {
            const existing = trackMenuProbe.existingFile
            openWithKeyboard(TrackDragCodec.encode([{ path: existing, subsong: 0 },
                                                    { path: existing, subsong: 1 }], "", 2), 2)
            verify(!menu.itemAt(menu.count - 1).visible)
        }

        function test_open_keys() {
            verify(menu.isOpenKey({ key: Qt.Key_Menu, modifiers: Qt.NoModifier }))
            verify(menu.isOpenKey({ key: Qt.Key_F10, modifiers: Qt.ShiftModifier }))
            verify(!menu.isOpenKey({ key: Qt.Key_F10, modifiers: Qt.NoModifier }))
            verify(!menu.isOpenKey({ key: Qt.Key_Return, modifiers: Qt.NoModifier }))
        }
    }
}
