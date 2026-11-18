.pragma library

// 音樂庫各清單共用的鍵盤操作與選取（T-25，DESIGN §7 清單的鍵盤操作）。ctx 是清單給的：
//   list       ListView 或 GridView（currentIndex、count、currentItem、positionViewAtIndex）
//   selection  TrackSelection
//   menu       TrackMenu
//   player     Player（核心的 QML 單例：enqueueNext、enqueueLast）
//   contain    ListView.Contain（positionViewAtIndex 的模式）
//   rowStep    上下鍵一次移幾格（清單 1，封面牆是欄數）
//   colStep    左右鍵一次移幾格（封面牆 1；清單 0＝不處理左右鍵）
//   activate   function(row)：Enter（曲目清單從這首播放，專輯開專輯詳細）

// 已選取的內容；沒有選取時先選目前列
function payload(ctx) {
    if (ctx.selection.count === 0 && ctx.list.currentIndex >= 0)
        ctx.selection.select(ctx.list.currentIndex)
    return ctx.selection.payloadJson()
}

// 移到第 row 列；extend 時從錨點選到這列（Shift），否則只選這列
function moveTo(ctx, row, extend) {
    const list = ctx.list
    if (list.count === 0)
        return
    const target = Math.max(0, Math.min(list.count - 1, row))
    list.currentIndex = target
    if (extend)
        ctx.selection.extend(target)
    else
        ctx.selection.select(target)
    list.positionViewAtIndex(target, ctx.contain)
}

// 選單鍵、Shift+F10：開在目前列下緣
function openMenuFromKeyboard(ctx) {
    const list = ctx.list
    if (list.currentIndex < 0)
        return
    ctx.selection.ensureSelected(list.currentIndex)
    ctx.menu.popupFrom(ctx.selection.payloadJson(), ctx.selection.trackCount, list.currentItem ?? list)
}

// 滑鼠右鍵：這列沒被選取時改成只選它，在游標位置開
function openMenuAt(ctx, row) {
    ctx.list.currentIndex = row
    ctx.list.forceActiveFocus()
    ctx.selection.ensureSelected(row)
    ctx.menu.popupAt(ctx.selection.payloadJson(), ctx.selection.trackCount)
}

// 點擊：Ctrl 切換、Shift 從錨點選到這列、都沒有時只選這列
function click(ctx, row) {
    ctx.list.currentIndex = row
    ctx.list.forceActiveFocus()
    ctx.selection.click(row)
}

// 處理了回傳 true（呼叫端設 event.accepted）
function handleKey(ctx, event) {
    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
    const current = ctx.list.currentIndex
    if (ctx.menu.isOpenKey(event)) {
        openMenuFromKeyboard(ctx)
        return true
    }
    switch (event.key) {
    case Qt.Key_Up:
        moveTo(ctx, current < 0 ? 0 : current - ctx.rowStep, shift)
        return true
    case Qt.Key_Down:
        moveTo(ctx, current < 0 ? 0 : current + ctx.rowStep, shift)
        return true
    case Qt.Key_Left:
        if (ctx.colStep === 0)
            return false
        moveTo(ctx, current < 0 ? 0 : current - ctx.colStep, shift)
        return true
    case Qt.Key_Right:
        if (ctx.colStep === 0)
            return false
        moveTo(ctx, current < 0 ? 0 : current + ctx.colStep, shift)
        return true
    case Qt.Key_A:
        if (!ctrl)
            return false
        ctx.selection.selectAll()
        return true
    case Qt.Key_Return:
    case Qt.Key_Enter:
        if (current < 0)
            return true
        if (shift)
            ctx.player.enqueueNext(payload(ctx))
        else if (ctrl)
            ctx.player.enqueueLast(payload(ctx))
        else
            ctx.activate(current)
        return true
    }
    return false
}

// 狀態列左側的說明（S-07）；沒有選取時空字串
function statusText(trackCount) {
    if (trackCount <= 0)
        return ""
    return qsTr("已選取 %1 首。拖到左側播放清單即可加入；右鍵可選「接著播放」或「加到佇列最後」。")
        .arg(Number(trackCount).toLocaleString(Qt.locale(), "f", 0))
}
