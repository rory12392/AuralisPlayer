pragma ComponentBehavior: Bound
import QtQuick

// 元件展示頁「控制項」：介面規範 §3 的按鈕與輸入元件，依清單順序。
Flow {
    id: section

    readonly property int columnWidth: width >= 2 * Theme.panelWidth + Theme.space6
                                       ? Math.floor((width - Theme.space6) / 2) : width
    property bool saving: true

    spacing: Theme.space6

    Panel {
        width: section.columnWidth
        title: "Button"
        Flow {
            width: parent.width
            spacing: Theme.space2
            Button { text: qsTr("加入音樂資料夾") }
            Button { text: qsTr("匯入播放清單"); variant: "solid" }
            Button { text: qsTr("刪除播放清單"); variant: "signal" }
            Button { text: qsTr("停用"); enabled: false }
            Button { text: qsTr("匯出播放清單"); busy: section.saving; onClicked: section.saving = !section.saving }
        }
    }

    Panel {
        width: section.columnWidth
        title: "IconButton"
        Row {
            spacing: Theme.space3
            IconButton { iconName: "previous"; text: qsTr("上一首"); size: 40 }
            IconButton { iconName: "play"; text: qsTr("播放"); variant: "round" }
            IconButton { iconName: "next"; text: qsTr("下一首"); size: 40 }
            IconButton { iconName: "add"; text: qsTr("新增播放清單") }
            IconButton { iconName: "panel"; text: qsTr("正在播放面板"); size: 40; checkable: true; checked: true }
            IconButton { iconName: "shuffle"; text: qsTr("隨機"); size: 40; enabled: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "SegmentedControl"
        Column {
            spacing: Theme.space3
            SegmentedControl {
                label: qsTr("檢視方式")
                model: [{ text: qsTr("封面牆") }, { text: qsTr("清單") }, { text: qsTr("詳細") }]
            }
            SegmentedControl {
                label: qsTr("DSD 輸出")
                currentIndex: 0
                model: [{ text: qsTr("DoP") }, { text: qsTr("原生 DSD"), enabled: false, reason: qsTr("需選 ASIO") },
                        { text: qsTr("轉 PCM 176.4 kHz") }]
            }
        }
    }

    Panel {
        width: section.columnWidth
        title: "TextField"
        Column {
            width: parent.width
            spacing: Theme.space4
            TextField { width: parent.width; label: qsTr("播放清單名稱"); text: qsTr("通勤") }
            TextField {
                width: parent.width
                label: qsTr("播放清單名稱")
                text: qsTr("通勤")
                errorText: qsTr("已經有叫「通勤」的播放清單，請換一個名稱。")
            }
            TextField { width: parent.width; label: qsTr("匯出位置"); placeholderText: qsTr("選擇資料夾"); enabled: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "SearchField"
        Column {
            width: parent.width
            spacing: Theme.space3
            SearchField { width: parent.width; placeholderText: qsTr("搜尋曲名、藝人、專輯"); findShortcut: false }
            SearchField { width: parent.width; placeholderText: qsTr("搜尋曲名、藝人、專輯"); text: "Bach"; findShortcut: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Chip"
        Row {
            spacing: Theme.space2
            Chip { label: qsTr("格式"); menu: formatMenu }
            Chip { label: qsTr("取樣率"); value: "96 kHz"; menu: formatMenu }
            Chip { label: qsTr("年份"); enabled: false }
        }
        Menu {
            id: formatMenu
            MenuItem { text: "FLAC" }
            MenuItem { text: "DSF" }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Checkbox"
        Column {
            spacing: Theme.space3
            Checkbox { text: qsTr("線上抓取歌詞"); description: qsTr("只連到 LRCLIB"); checked: true }
            Checkbox { text: qsTr("監看音樂資料夾") }
            Checkbox { text: qsTr("停用的選項"); enabled: false; checked: true }
        }
    }

    Panel {
        width: section.columnWidth
        title: "RadioRow"
        Column {
            width: parent.width
            RadioRow {
                width: parent.width; text: qsTr("WASAPI 獨占"); detail: "USB DAC"
                note: qsTr("位元完美"); noteTone: "amber"; checked: true
            }
            RadioRow { width: parent.width; text: qsTr("ASIO"); detail: "USB DAC ASIO"; note: qsTr("位元完美，可原生 DSD") }
            RadioRow {
                width: parent.width; text: qsTr("WASAPI 獨占"); detail: qsTr("喇叭")
                note: qsTr("裝置被其他程式使用中"); noteTone: "signal"
            }
            RadioRow {
                width: parent.width; text: qsTr("WASAPI 共享"); detail: qsTr("耳機")
                unavailableReason: qsTr("裝置已拔除")
            }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Select"
        Row {
            spacing: Theme.space3
            Select { label: qsTr("排序"); model: [qsTr("專輯藝人"), qsTr("年份"), qsTr("加入時間")] }
            Select { label: qsTr("取樣率"); model: ["176.4 kHz"]; enabled: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Slider"
        Column {
            width: parent.width
            spacing: Theme.space3
            Slider { width: parent.width; label: qsTr("進度"); variant: "progress"; value: 0.42 }
            Slider { width: parent.width / 2; label: qsTr("音量"); value: 0.85; stepSize: 0.01 }
            Slider { width: parent.width / 2; label: qsTr("音量"); value: 1; enabled: false }
        }
    }

    Panel {
        width: section.columnWidth
        title: "Tabs"
        Tabs {
            width: parent.width
            model: [qsTr("正在播放"), qsTr("歌詞"), qsTr("佇列 12")]
        }
    }
}
