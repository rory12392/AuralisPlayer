pragma Singleton
import QtQuick

// 設計 token（介面規範 §2.1）。只有這個資料夾可以寫色碼與數值，其他 QML 一律用這裡的 token。
QtObject {
    // ThemeController 由 C++ 在引擎建立前決定深淺色
    readonly property bool dark: ThemeController.dark

    // 色彩（Void 深色 : Paper 淺色）
    readonly property color bg:            dark ? "#050505" : "#f4f2ee"
    readonly property color surface:       dark ? "#0e0e0e" : "#ffffff"
    readonly property color surfaceRaised: dark ? "#171717" : "#e9e6e0"
    readonly property color line:          dark ? "#2a2a2a" : "#d6d1c8"
    readonly property color lineStrong:    dark ? "#666666" : "#7a756c"
    readonly property color ink:           dark ? "#f2f0eb" : "#0a0a0a"
    readonly property color inkMuted:      dark ? "#9a968e" : "#57534c"
    readonly property color inkFaint:      dark ? "#6b6862" : "#8a857c"
    readonly property color amber:         dark ? "#e0a34a" : "#8f5810"
    // token 名稱照介面規範；Qt 6.12 的 qmllint 把 on 開頭當成 amber 的訊號處理器名稱，這個屬性不是
    readonly property color onAmber:       dark ? "#050505" : "#ffffff" // qmllint disable signal-handler-name-clash
    readonly property color amberDeep:     dark ? "#5a3a14" : "#ecd3a8"
    readonly property color signal:        dark ? "#e5584b" : "#b3261e"
    readonly property color focus:         ink
    readonly property color transparent:   "transparent"
    readonly property color coverMark:     "#73ffffff"
    // 全螢幕：等於一般 token，跟著全螢幕的有效主題（M-07，DESIGN §2.3）；新程式直接用一般 token
    readonly property color fsInk:      ink
    readonly property color fsInkMuted: inkMuted
    readonly property color fsLine:     line
    readonly property color fsAmber:    amber

    // 字級（pixelSize）與行高
    readonly property int textDisplay: 44;  readonly property int lineDisplay: 56
    readonly property int textTitle:   32;  readonly property int lineTitle:   40
    readonly property int textLyric:   22;  readonly property int lineLyric:   32
    readonly property int textHeading: 20;  readonly property int lineHeading: 28
    readonly property int textReadout: 15;  readonly property int lineReadout: 20
    readonly property int textBody:    14;  readonly property int lineBody:    20
    readonly property int textSmall:   12;  readonly property int lineSmall:   16
    readonly property int textLabel:   12;  readonly property int lineLabel:   16
    readonly property real labelTracking: 1.5
    readonly property real brandTracking: 4.0
    readonly property int weightLight: 300; readonly property int weightRegular: 400; readonly property int weightMedium: 500

    // 字體：基本字族（Jost → Microsoft JhengHei UI → Yu Gothic UI → Segoe UI；
    // IBM Plex Mono → Microsoft JhengHei UI → Consolas）
    readonly property font sans: ThemeController.sansFont
    readonly property font mono: ThemeController.monoFont
    // 字型 token：QML 不能在 font: 之後再設 font.pixelSize，所以每個用途一個完整的字型（§2.4）
    readonly property font heading: ThemeController.makeFont(false, textHeading, weightRegular)
    readonly property font body:    ThemeController.makeFont(false, textBody, weightRegular)
    readonly property font bodyMedium: ThemeController.makeFont(false, textBody, weightMedium)
    readonly property font small:   ThemeController.makeFont(false, textSmall, weightRegular)
    readonly property font label:   ThemeController.makeFont(false, textLabel, weightMedium, labelTracking, true)
    readonly property font brand:   ThemeController.makeFont(false, textLabel, weightMedium, brandTracking, true)
    readonly property font monoSmall:   ThemeController.makeFont(true, textSmall, weightRegular)
    readonly property font monoReadout: ThemeController.makeFont(true, textReadout, weightRegular)
    readonly property font title:   ThemeController.makeFont(false, textTitle, weightLight)
    readonly property font lyric:   ThemeController.makeFont(false, textLyric, weightLight)
    readonly property font display: ThemeController.makeFont(false, textDisplay, weightLight)

    // 間距（4px 為單位）
    readonly property int space1: 4;  readonly property int space2: 8;  readonly property int space3: 12
    readonly property int space4: 16; readonly property int space6: 24; readonly property int space8: 32
    readonly property int space12: 48; readonly property int space24: 96

    // 尺寸
    readonly property int controlHeight: 32;  readonly property int chipHeight: 26
    readonly property int rowHeight: 44;      readonly property int rowHeightCompact: 36
    readonly property int headerHeight: 56;   readonly property int barHeight: 88
    readonly property int sidebarWidth: 232;  readonly property int panelWidth: 340
    readonly property int hitMin: 24;         readonly property int hairline: 1
    readonly property int coverThumb: 32;     readonly property int coverBar: 48
    readonly property int focusWidth: 2;      readonly property int statusBarHeight: 36
    readonly property int filterBarHeight: 44
    readonly property int windowMinWidth: 960; readonly property int windowMinHeight: 640

    // 圓角：只有直角與正圓
    readonly property int radius0: 0;  readonly property int radiusFull: 9999

    // 動畫（毫秒）；系統關掉「顯示動畫」時全部是 0。全螢幕中換主題的那一刻 motionState 也是 0
    // （ThemeController.instantThemeSwitch）：共用元件的顏色不淡入，不會有一格深字配深底（T-121）
    readonly property int motionState: ThemeController.reduceMotion || ThemeController.instantThemeSwitch ? 0 : 150
    readonly property int motionPanel: ThemeController.reduceMotion ? 0 : 400
    // 全螢幕底色在換深淺色時直接換成另一版，不走過渡（DESIGN §2.3），換歌時才過渡
    readonly property int motionTint:  ThemeController.reduceMotion || ThemeController.instantThemeSwitch ? 0 : 600
    readonly property int noticeTimeout: 6000

    // 唯一的陰影：琥珀光暈，只有 StatusTag 的琥珀指示點可以用
    readonly property color glowAmber: dark ? "#38e0a34a" : "#2e8f5810"
    readonly property int glowRadius:  dark ? 48 : 32
}
