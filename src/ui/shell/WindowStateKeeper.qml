import QtQuick

// 主視窗的大小、位置與最大化（介面規範 §7）：啟動時 restore() 套用 ShellState 記住的狀態一次，之後
// 不用繫結覆寫使用者在標題列、Win+↑ 這類系統操作做的最大化或還原（原本 visibility 繫結在 ShellState 與
// Navigator 上，開關面板、切頁都會重算成一般視窗，T-92）。全螢幕進出後回到進入前的最大化或一般視窗；
// 關閉時 save() 記下最大化狀態，一般視窗時另外記位置與大小。
QtObject {
    id: keeper

    required property Window target
    // 全螢幕頁（Navigator.fullscreen）
    property bool fullscreen: false
    // 全螢幕以外最後一次的狀態：最大化或一般視窗（最小化不算）
    property bool maximized: false

    function stateVisibility() {
        return keeper.fullscreen ? Window.FullScreen : keeper.maximized ? Window.Maximized : Window.Windowed
    }

    function restore() {
        keeper.target.width = ShellState.windowWidth
        keeper.target.height = ShellState.windowHeight
        if (ShellState.hasPosition) {
            keeper.target.x = ShellState.windowX
            keeper.target.y = ShellState.windowY
        }
        keeper.maximized = ShellState.maximized
        keeper.target.visibility = keeper.stateVisibility()
    }

    function save() {
        ShellState.maximized = keeper.maximized
        if (keeper.target.visibility === Window.Windowed) {
            ShellState.windowX = keeper.target.x
            ShellState.windowY = keeper.target.y
            ShellState.windowWidth = keeper.target.width
            ShellState.windowHeight = keeper.target.height
        }
        ShellState.save()
    }

    // 包含 (x, y) 的螢幕（screens 是 Application.screens 這類有 virtualX、virtualY、width、height 的
    // 物件），都不包含時 null
    function screenAt(screens, x, y) {
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (x >= s.virtualX && x < s.virtualX + s.width && y >= s.virtualY && y < s.virtualY + s.height)
                return s
        }
        return null
    }

    function applyState() {
        // 全螢幕開在主視窗目前所在的螢幕（視窗中心所在的那個），不是上次全螢幕或啟動時的螢幕
        if (keeper.fullscreen) {
            const t = keeper.target
            const here = keeper.screenAt(Application.screens,t.x + t.width / 2, t.y + t.height / 2)
            if (here !== null && here.name !== t.screen.name)
                t.screen = here
        }
        keeper.target.visibility = keeper.stateVisibility()
    }

    // 進出全螢幕不畫出跑版的一格（P-LYR-11）：原生視窗改大小時，系統在新的一格畫出來之前沿用上一格
    // 畫面（進入時是一般頁擠在全螢幕的角落，離開時是全螢幕頁被裁成視窗大小）。所以換頁後先用整片
    // 底色（新狀態的 Theme.bg）蓋住視窗、等這一格交換出去，再改視窗狀態；視窗狀態到位後再畫一格
    // （新頁面在遮罩底下排好新尺寸）才拿掉遮罩。全靠 frameSwapped 數格子，不用計時器猜時間
    // 0：沒有在換；1：等遮罩那一格交換出去；2：等視窗狀態到位；3：到位後再等一格
    property int phase: 0
    // 第 1 段要等兩格：換頁時可能已經有一格（沒有遮罩的）在畫，先交換出去的是它
    property int coverFrames: 0
    // 視窗狀態一直沒到位時（例：系統不讓改）最多等這麼多格就拿掉遮罩，不會一直蓋著
    readonly property int settleFrameLimit: 10
    property int settleFrames: 0

    onFullscreenChanged: {
        const t = keeper.target
        if (!t.visible || t.visibility === Window.Minimized) {
            keeper.phase = 0
            keeper.applyState()
            return
        }
        keeper.phase = 1
        keeper.coverFrames = 0
        t.update()
    }

    readonly property Rectangle cover: Rectangle {
        objectName: "windowStateCover"
        parent: keeper.target ? keeper.target.contentItem : null
        anchors.fill: parent
        z: 1000000
        visible: keeper.phase !== 0
        color: Theme.bg
    }

    readonly property Connections frames: Connections {
        target: keeper.target
        enabled: keeper.phase !== 0
        function onFrameSwapped() {
            const t = keeper.target
            if (keeper.phase === 1) {
                keeper.coverFrames += 1
                if (keeper.coverFrames < 2) {
                    t.update()
                    return
                }
                keeper.phase = 2
                keeper.settleFrames = 0
                keeper.applyState()
            } else if (keeper.phase === 2) {
                keeper.settleFrames += 1
                if (t.visibility === keeper.stateVisibility() || keeper.settleFrames >= keeper.settleFrameLimit)
                    keeper.phase = 3
            } else {
                keeper.phase = 0
            }
            t.update()
        }
    }

    // 保險：視窗被別的視窗整個蓋住時 Qt 不畫新的格子、收不到 frameSwapped（例：從背景行程開的
    // --soak --fullscreen），這時跑版也看不到，直接改視窗狀態、拿掉遮罩，不會卡在遮罩。正常換狀態
    // 只要幾格，不會等到它
    readonly property Timer watchdog: Timer {
        interval: 500
        running: keeper.phase !== 0
        onTriggered: {
            if (keeper.phase === 1)
                keeper.applyState()
            keeper.phase = 0
        }
    }

    readonly property Connections watcher: Connections {
        target: keeper.target
        function onVisibilityChanged(visibility) {
            if (keeper.fullscreen)
                return
            if (visibility === Window.Maximized)
                keeper.maximized = true
            else if (visibility === Window.Windowed)
                keeper.maximized = false
        }
    }
}
