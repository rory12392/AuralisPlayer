.pragma library

// 播放清單頁曲目清單的鍵盤操作（介面規範 §7 清單的鍵盤操作，寫法照音樂庫清單，不 import 音樂庫模組）。
// ctx 是清單給的：
//   list    ListView
//   tracks  PlaylistTracks（選取、payloadJson、排序、移除）
//   menu    TrackMenu
//   player  Player（核心的 QML 單例：enqueueNext、enqueueLast）
//   contain ListView.Contain
// 播放清單頁多的：Delete＝從播放清單移除選取的列；Ctrl＋上下鍵＝把選取的列往上／往下移一格
// （同佇列面板的排序鍵）。

// 已選取的內容；沒有選取時先選目前列
function payload(ctx) {
    if (ctx.tracks.selectedCount === 0 && ctx.list.currentIndex >= 0)
        ctx.tracks.select(ctx.list.currentIndex)
    return ctx.tracks.payloadJson()
}

function moveTo(ctx, row, extend) {
    const list = ctx.list
    if (list.count === 0)
        return
    const target = Math.max(0, Math.min(list.count - 1, row))
    list.currentIndex = target
    if (extend)
        ctx.tracks.extend(target)
    else
        ctx.tracks.select(target)
    list.positionViewAtIndex(target, ctx.contain)
}

// 選單鍵、Shift+F10：開在目前列下緣
function openMenuFromKeyboard(ctx) {
    const list = ctx.list
    if (list.currentIndex < 0)
        return
    ctx.tracks.ensureSelected(list.currentIndex)
    ctx.menu.popupFrom(ctx.tracks.payloadJson(), ctx.tracks.selectedCount, list.currentItem ?? list)
}

// 滑鼠右鍵：這列沒被選取時改成只選它，在游標位置開
function openMenuAt(ctx, row) {
    ctx.list.currentIndex = row
    ctx.list.forceActiveFocus()
    ctx.tracks.ensureSelected(row)
    ctx.menu.popupAt(ctx.tracks.payloadJson(), ctx.tracks.selectedCount)
}

function click(ctx, row) {
    ctx.list.currentIndex = row
    ctx.list.forceActiveFocus()
    ctx.tracks.click(row)
}

// Ctrl＋上下鍵：目前列跟著選取一起移
function moveSelection(ctx, delta) {
    const list = ctx.list
    const current = list.currentIndex
    if (current >= 0 && ctx.tracks.selectedCount === 0)
        ctx.tracks.select(current)
    const moving = current >= 0 && ctx.tracks.isSelected(current)
    if (!ctx.tracks.moveSelectionBy(delta))
        return
    if (moving && ctx.tracks.isSelected(current + delta)) {
        list.currentIndex = current + delta
        list.positionViewAtIndex(current + delta, ctx.contain)
    }
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
        if (ctrl)
            moveSelection(ctx, -1)
        else
            moveTo(ctx, current < 0 ? 0 : current - 1, shift)
        return true
    case Qt.Key_Down:
        if (ctrl)
            moveSelection(ctx, 1)
        else
            moveTo(ctx, current < 0 ? 0 : current + 1, shift)
        return true
    case Qt.Key_A:
        if (!ctrl)
            return false
        ctx.tracks.selectAll()
        return true
    case Qt.Key_Delete:
        if (current >= 0 && ctx.tracks.selectedCount === 0)
            ctx.tracks.select(current)
        if (ctx.tracks.removeSelected())
            ctx.list.currentIndex = Math.min(current, ctx.list.count - 1)
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
            ctx.tracks.playFrom(current)
        return true
    }
    return false
}
