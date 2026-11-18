.pragma library

// TrackRow 與 ListHeader 共用的欄位清單與欄寬計算，讓表頭和每一列的欄位對齊（介面規範 §3）。
// 欄位清單是陣列，每欄一個物件：
//   id        欄的識別（同一份清單裡不重複）
//   title     表頭文字（可以空白，例：拖曳把手欄）
//   width     固定寬（px）；或 weight：分剩下的寬度（比例，預設 1）。兩者擇一
//   align     "left"（預設）或 "right"
//   mono      true＝等寬字；沒寫時 duration、format、sampleRate 是等寬、其他不是
//   role      值從列的 record[role] 取（預設＝id）；或 value：function(record) → 字串
//             id 是六個固定槽位（title、artist、album、duration、format、sampleRate）而且沒寫 role、value 時，
//             值照舊從 TrackRow 的同名屬性取
//   minWidth  拖曳與窄視窗時的最小寬（預設 48；固定寬比 48 小的欄就是它的寬）
//   priority  寬度不夠時的隱藏順序，數字大的先藏（預設：曲名 0、藝人 1、長度 2、取樣率 3、專輯 4、格式 5、
//             其他欄 10）；曲名欄（fill）永遠不藏
//   fill      true＝這一欄吃剩下的寬度、拖曳時不會變成固定寬；沒寫時是 id "title"，再沒有就是第一個 weight 欄
//   sortable  false＝表頭這欄不是按鈕（不進 Tab 順序）
//   optional  true＝欄位目錄裡預設不顯示的欄（restore 沒有存檔時用）
// 頁面保存與還原用 save()／restore()；元件本身不碰設定。

// 列的左側：播放標記欄（6px 方點置中）與封面縮圖之後才開始第一欄
const markWidth = 16
// 欄的預設最小寬
const minColumnWidth = 48

const fixedSlots = ["title", "artist", "album", "duration", "format", "sampleRate"]
const monoSlots = ["duration", "format", "sampleRate"]
// 介面規範 §4 的欄位優先順序：曲名 > 藝人 > 長度 > 取樣率 > 專輯 > 格式
const defaultPriority = { title: 0, artist: 1, duration: 2, sampleRate: 3, album: 4, format: 5 }

function defaultColumns() {
    return [
        { id: "title", title: qsTr("曲名"), weight: 3 },
        { id: "artist", title: qsTr("藝人"), weight: 2 },
        { id: "album", title: qsTr("專輯"), weight: 2 },
        { id: "duration", title: qsTr("長度"), width: 64, align: "right" },
        { id: "format", title: qsTr("格式"), width: 64 },
        { id: "sampleRate", title: qsTr("取樣率"), width: 136 }
    ]
}

function isFixedSlot(id) {
    return fixedSlots.indexOf(id) >= 0
}

function isMono(column) {
    return column.mono ?? monoSlots.indexOf(column.id) >= 0
}

function minWidthOf(column) {
    if (column.minWidth !== undefined)
        return column.minWidth
    return column.width !== undefined ? Math.min(column.width, minColumnWidth) : minColumnWidth
}

// 吃剩下寬度的欄：fill: true，沒有時 id "title"，再沒有時第一個 weight 欄；都沒有時 -1
function fillIndex(columns) {
    let index = columns.findIndex(column => column.fill === true)
    if (index < 0)
        index = columns.findIndex(column => column.id === "title" && column.width === undefined)
    if (index < 0)
        index = columns.findIndex(column => column.width === undefined)
    return index
}

function priorityOf(columns, index) {
    if (index === fillIndex(columns))
        return -1
    const column = columns[index]
    return column.priority ?? defaultPriority[column.id] ?? 10
}

// 依可用寬度分配：固定寬照給，剩下的依 weight 分；shown[i] 為 false 的欄寬 0
function distribute(columns, shown, available, spacing) {
    let count = 0
    let fixed = 0
    let weights = 0
    columns.forEach((column, i) => {
        if (!shown[i])
            return
        ++count
        if (column.width !== undefined)
            fixed += column.width
        else
            weights += column.weight ?? 1
    })
    fixed += spacing * Math.max(0, count - 1)
    const rest = Math.max(0, available - fixed)
    let fits = fixed <= available
    const result = columns.map((column, i) => {
        if (!shown[i])
            return 0
        if (column.width !== undefined)
            return column.width
        const width = Math.floor(rest * (column.weight ?? 1) / Math.max(1, weights))
        if (width < minWidthOf(column))
            fits = false
        return width
    })
    return { widths: result, fits: fits }
}

// 每一欄的寬度（0＝寬度不夠、這一欄藏起來；介面規範 §4：不出現橫向捲軸）。放得下時和以前一樣：
// 固定寬照給，剩下的依 weight 分；放不下（固定寬加 weight 欄的最小寬超過可用寬度）時依 priority 一次藏一欄
function widths(columns, available, spacing) {
    const shown = columns.map(() => true)
    for (;;) {
        const layout = distribute(columns, shown, available, spacing)
        if (layout.fits)
            return layout.widths
        let drop = -1
        let dropPriority = -1
        let remaining = 0
        shown.forEach((visible, i) => {
            if (!visible)
                return
            ++remaining
            const priority = priorityOf(columns, i)
            if (priority >= dropPriority && priority >= 0) {
                drop = i
                dropPriority = priority
            }
        })
        if (drop < 0 || remaining <= 1)
            return layout.widths
        shown[drop] = false
    }
}

// 每一欄右緣相對於第一欄左緣的位置（藏起來的欄是 -1），表頭的分隔線放在這裡加半個間距
function rightEdges(widths, spacing) {
    let x = 0
    let first = true
    return widths.map(width => {
        if (width <= 0)
            return -1
        if (!first)
            x += spacing
        first = false
        x += width
        return x
    })
}

// 最右邊顯示中的欄（沒有時 -1）
function lastShown(widths) {
    for (let i = widths.length - 1; i >= 0; --i) {
        if (widths[i] > 0)
            return i
    }
    return -1
}

// index 之後下一個顯示中的欄（沒有時 -1）
function nextShown(widths, index) {
    for (let i = index + 1; i < widths.length; ++i) {
        if (widths[i] > 0)
            return i
    }
    return -1
}

// 拖第 index 欄右邊的分隔線時改哪一欄：一般是這一欄（往右拖變寬）；這一欄是 fill 時改下一欄（往右拖變窄），
// 曲名欄才會一直吃剩下的寬度
function resizeTarget(columns, widths, index) {
    const next = nextShown(widths, index)
    if (next < 0)
        return { index: -1, sign: 1 }
    if (index === fillIndex(columns))
        return { index: next, sign: -1 }
    return { index: index, sign: 1 }
}

// 第 target 欄拖曳時的寬度範圍：下限是 minWidth；上限是其他 weight 欄都還保有最小寬時的寬度
function resizeLimits(columns, widths, target, available, spacing) {
    let count = 0
    let fixedOthers = 0
    let weights = 0
    const weighted = []
    widths.forEach((width, i) => {
        if (width <= 0)
            return
        ++count
        if (i === target)
            return
        if (columns[i].width !== undefined) {
            fixedOthers += width
        } else {
            weights += columns[i].weight ?? 1
            weighted.push(i)
        }
    })
    fixedOthers += spacing * Math.max(0, count - 1)
    let required = 0
    for (const i of weighted)
        required = Math.max(required, Math.ceil(minWidthOf(columns[i]) * weights / (columns[i].weight ?? 1)) + 1)
    const min = minWidthOf(columns[target])
    return { min: min, max: Math.max(min, Math.floor(available - fixedOthers - required)) }
}

function withWidth(column, width) {
    const result = Object.assign({}, column)
    delete result.weight
    result.width = Math.round(width)
    return result
}

// 把 id 那一欄改成固定寬 width，回傳新的欄位清單（頁面在 ListHeader.columnResized 裡換掉自己的清單）
function resized(columns, id, width) {
    return columns.map(column => column.id === id ? withWidth(column, width) : column)
}

// 保存格式：JSON 字串，陣列依顯示順序，每欄 { id }，固定寬的欄加 width，例：
// [{"id":"title"},{"id":"artist","width":180},{"id":"duration","width":64}]
function save(columns) {
    return JSON.stringify(columns.map(column => column.width !== undefined ? { id: column.id, width: column.width }
                                                                         : { id: column.id }))
}

// 依保存的字串從欄位目錄（catalog：所有可選的欄）還原欄位清單：照存檔的順序、套用寬度（不小於 minWidth）；
// 目錄沒有的 id、重複的 id 略過；fill 欄不套寬度，存檔裡沒有時放回最前面；
// 存檔空白、壞掉或一欄都對不上時，回傳目錄裡沒標 optional 的欄
function restore(catalog, json) {
    let saved = []
    try {
        saved = JSON.parse(json)
    } catch (error) {
        saved = []
    }
    const fill = catalog[fillIndex(catalog)]
    const result = []
    if (Array.isArray(saved)) {
        for (const entry of saved) {
            if (!entry || typeof entry.id !== "string" || result.some(column => column.id === entry.id))
                continue
            const column = catalog.find(candidate => candidate.id === entry.id)
            if (!column)
                continue
            const width = Number(entry.width)
            result.push(column !== fill && entry.width !== undefined && width > 0
                        ? withWidth(column, Math.max(minWidthOf(column), width)) : column)
        }
    }
    if (result.length === 0)
        return catalog.filter(column => column.optional !== true)
    if (fill && !result.some(column => column.id === fill.id))
        result.unshift(fill)
    return result
}
