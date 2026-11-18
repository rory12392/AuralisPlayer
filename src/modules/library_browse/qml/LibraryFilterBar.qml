pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import Auralis.Ui
import Auralis.Lib

// 篩選列（畫面規格 S-03「篩選列」，S-07 共用）：標籤「篩選」、Chip「格式」「取樣率」「年份」「藝人」，
// 已套用時 Chip 顯示值；可疊加、和搜尋同時生效（AC-09-3）。選項是 LibrarySearch 的 *Options
// （整個音樂庫出現過的值）。右側等寬的 countText 由頁面給（「<n> 首符合」，專輯頁給「<n> 張專輯」）。
Rectangle {
    id: bar

    property string countText

    implicitHeight: Theme.filterBarHeight
    color: Theme.bg

    Component.onCompleted: {
        if (!LibrarySearch.optionsLoaded)
            LibrarySearch.loadOptions()
    }

    function sampleRateLabel(rate) {
        const options = LibrarySearch.sampleRateOptions
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === rate)
                return options[i].text
        }
        return rate > 0 ? String(rate) : ""
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.space6
        anchors.right: count.left
        anchors.rightMargin: Theme.space3
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2
        clip: true

        Text {
            anchors.verticalCenter: parent.verticalCenter
            rightPadding: Theme.space1
            text: qsTr("篩選")
            font: Theme.label
            color: Theme.inkMuted
        }

        Chip {
            objectName: "filterFormat"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("格式")
            // 沒有選項時（例：音樂庫沒有年份資料）停用，已套用的照樣可以清掉
            enabled: applied || LibrarySearch.formatOptions.length > 0
            value: LibrarySearch.format
            onCleared: LibrarySearch.format = ""
            menu: Menu {
                id: formatMenu
                Instantiator {
                    model: LibrarySearch.formatOptions
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: LibrarySearch.format = modelData
                    }
                    onObjectAdded: (index, object) => formatMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => formatMenu.removeItem(object)
                }
            }
        }

        Chip {
            objectName: "filterSampleRate"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("取樣率")
            enabled: applied || LibrarySearch.sampleRateOptions.length > 0
            value: LibrarySearch.sampleRate > 0 ? bar.sampleRateLabel(LibrarySearch.sampleRate) : ""
            onCleared: LibrarySearch.sampleRate = 0
            menu: Menu {
                id: rateMenu
                Instantiator {
                    model: LibrarySearch.sampleRateOptions
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.text
                        onTriggered: LibrarySearch.sampleRate = modelData.value
                    }
                    onObjectAdded: (index, object) => rateMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => rateMenu.removeItem(object)
                }
            }
        }

        Chip {
            objectName: "filterYear"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("年份")
            enabled: applied || LibrarySearch.yearOptions.length > 0
            value: LibrarySearch.year > 0 ? String(LibrarySearch.year) : ""
            onCleared: LibrarySearch.year = 0
            menu: Menu {
                id: yearMenu
                Instantiator {
                    model: LibrarySearch.yearOptions
                    delegate: MenuItem {
                        required property var modelData
                        text: String(modelData)
                        onTriggered: LibrarySearch.year = modelData
                    }
                    onObjectAdded: (index, object) => yearMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => yearMenu.removeItem(object)
                }
            }
        }

        Chip {
            objectName: "filterArtist"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("藝人")
            enabled: applied || LibrarySearch.artistOptions.length > 0
            value: LibrarySearch.artist
            onCleared: LibrarySearch.artist = ""
            menu: Menu {
                id: artistMenu
                Instantiator {
                    model: LibrarySearch.artistOptions
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: LibrarySearch.artist = modelData
                    }
                    onObjectAdded: (index, object) => artistMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => artistMenu.removeItem(object)
                }
            }
        }

        // 選項讀取失敗（介面規範 §6「操作失敗」：發生什麼＋能做什麼）：Chip 沒有選項所以停用，
        // 旁邊用 signal 的 StatusTag 說原因，「重試」再讀一次
        StatusTag {
            objectName: "filterOptionsError"
            anchors.verticalCenter: parent.verticalCenter
            visible: LibrarySearch.optionsError.length > 0
            variant: "signal"
            text: LibrarySearch.optionsError
        }

        Button {
            objectName: "filterOptionsRetry"
            anchors.verticalCenter: parent.verticalCenter
            visible: LibrarySearch.optionsError.length > 0
            text: qsTr("重試")
            onClicked: LibrarySearch.loadOptions()
        }
    }

    Text {
        id: count
        objectName: "filterCount"
        anchors.right: parent.right
        anchors.rightMargin: Theme.space6
        anchors.verticalCenter: parent.verticalCenter
        text: bar.countText
        font: Theme.monoSmall
        color: Theme.inkMuted
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Theme.hairline
        color: Theme.line
    }
}
