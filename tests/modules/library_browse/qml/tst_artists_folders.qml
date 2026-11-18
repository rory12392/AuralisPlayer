import QtQuick
import QtTest
import Auralis.Ui
import Auralis.Lib

// 藝人頁（S-05）與資料夾頁（S-06）的鍵盤走查（T-23）：Tab 進左欄、上下鍵換選取（右邊跟著換）；
// 資料夾樹的右鍵展開（已展開時移到第一個子資料夾）、左鍵收合（已收合時移到父資料夾）；
// 右邊清單 Enter 從這首播放；拖曳分隔線調整左欄寬度。
Item {
    id: root
    width: 1100
    height: 720

    function focusItem() {
        return root.Window.activeFocusItem
    }

    function find(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            const found = find(kids[i], name)
            if (found)
                return found
        }
        return null
    }

    Button { id: start; text: "起點" }

    ArtistsPage {
        id: artistsPage
        y: 40
        width: 1000
        height: 600
        visible: false
    }

    FoldersPage {
        id: foldersPage
        y: 40
        width: 1000
        height: 600
        visible: false
    }

    TestCase {
        name: "ArtistsPage"
        when: windowShown

        function init() {
            artistsPage.visible = true
            foldersPage.visible = false
            LibraryAlbums.view = "grid"
            LibraryAlbums.sort = "added"
            fakePlayback.reset(0, -1)
            tryVerify(() => LibraryArtists.loaded && LibraryArtists.artistCount === 8, 5000)
            LibraryArtists.select(0)
            start.forceActiveFocus()
        }

        function test_keyboard() {
            const list = find(artistsPage, "artistList")
            verify(list.visible)
            compare(list.width, 280)
            keyClick(Qt.Key_Tab)
            verify(list.activeFocus) // 焦點在清單（ListView 把它交給目前列，按鍵仍由清單處理）
            compare(LibraryArtists.selectedRow, 0)
            compare(LibraryArtists.count, 8) // 全部藝人
            compare(artistsPage.statusText, "8 張專輯")

            keyClick(Qt.Key_Down)
            compare(LibraryArtists.selectedRow, 1)
            compare(LibraryArtists.selectedArtist, "藝人 1")
            compare(LibraryArtists.count, 1)
            const grid = find(artistsPage, "albumGrid")
            tryCompare(grid, "count", 1)

            keyClick(Qt.Key_Down)
            compare(LibraryArtists.selectedArtist, "藝人 2")
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Up)
            compare(LibraryArtists.selectedRow, 0)
        }

        function test_currentView() {
            // 右邊用專輯頁目前的檢視方式
            LibraryAlbums.view = "list"
            tryVerify(() => find(artistsPage, "albumList") !== null)
            LibraryAlbums.view = "detail"
            tryVerify(() => find(artistsPage, "albumDetailList") !== null)
            LibraryAlbums.view = "grid"
        }

        function test_playAlbum() {
            LibraryArtists.select(2)
            compare(LibraryArtists.selectedArtist, "藝人 2")
            verify(LibraryArtists.playAlbum(0))
            compare(fakePlayback.calls, ["playFrom 0"])
            compare(fakePlayback.paths, ["C:/t/2-1.flac", "C:/t/2-2.flac"])
        }
    }

    TestCase {
        name: "FoldersPage"
        when: windowShown

        function init() {
            artistsPage.visible = false
            foldersPage.visible = true
            fakePlayback.reset(0, -1)
            tryVerify(() => LibraryFolders.loaded && LibraryFolders.tracksLoaded, 5000)
            // 回到初始狀態：收合 ACG、選根
            const tree = find(foldersPage, "folderTree")
            LibraryFolders.collapse(1)
            LibraryFolders.select(0)
            tryVerify(() => LibraryFolders.tracksLoaded)
            start.forceActiveFocus()
        }

        // 看得到的列的名稱（讀模型：收合後被移除的列，委派可能還沒銷毀）
        function names() {
            const out = []
            const model = LibraryFolders.tree
            const nameRole = Qt.UserRole + 2 // FoldersModel::NameRole
            for (let i = 0; i < model.rowCount(); ++i)
                out.push(model.data(model.index(i, 0), nameRole))
            return out
        }

        function test_treeKeyboard() {
            const tree = find(foldersPage, "folderTree")
            verify(tree.visible)
            compare(tree.width, 320)
            compare(names(), ["t", "ACG", "其他"]) // 根預設展開一層
            keyClick(Qt.Key_Tab)
            verify(tree.activeFocus)
            compare(LibraryFolders.selectedPath, "C:\\t")
            compare(LibraryFolders.trackCount, 16) // 含子資料夾
            compare(foldersPage.statusText, "16 首")

            keyClick(Qt.Key_Down)
            compare(LibraryFolders.selectedPath, "C:\\t\\ACG")
            tryVerify(() => LibraryFolders.tracksLoaded && LibraryFolders.trackCount === 8)

            keyClick(Qt.Key_Right) // 展開
            compare(names(), ["t", "ACG", "Anime", "其他"])
            compare(tree.currentIndex, 1)
            keyClick(Qt.Key_Right) // 已展開：移到第一個子資料夾
            compare(tree.currentIndex, 2)
            compare(LibraryFolders.selectedPath, "C:\\t\\ACG\\Anime")

            keyClick(Qt.Key_Left) // 沒有子資料夾：移到父資料夾
            compare(tree.currentIndex, 1)
            keyClick(Qt.Key_Left) // 收合
            compare(names(), ["t", "ACG", "其他"])
            compare(tree.currentIndex, 1)
            keyClick(Qt.Key_Left) // 已收合：移到父資料夾
            compare(tree.currentIndex, 0)
            keyClick(Qt.Key_Return) // Enter 切換展開：收合根
            compare(names(), ["t"])
            keyClick(Qt.Key_Return)
            compare(names(), ["t", "ACG", "其他"])
        }

        function test_playFromList() {
            LibraryFolders.select(2) // 其他：專輯 5–8
            tryVerify(() => LibraryFolders.tracksLoaded && LibraryFolders.trackCount === 8)
            const list = find(foldersPage, "folderTrackList")
            verify(list.visible)
            list.forceActiveFocus()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(fakePlayback.calls, ["playFrom 1"])
            compare(fakePlayback.paths.length, 8)
            compare(fakePlayback.paths[0], "C:/t/5-1.flac")
        }

        function test_dragPane() {
            const handle = find(foldersPage, "folderPaneHandle")
            const before = LibraryFolders.paneWidth
            // 座標用頁面的（把手會跟著左欄寬度移動）
            const x = handle.mapToItem(foldersPage, handle.width / 2, 0).x
            mousePress(foldersPage, x, 100)
            mouseMove(foldersPage, x + 30, 100, -1, Qt.LeftButton)
            mouseMove(foldersPage, x + 60, 100, -1, Qt.LeftButton)
            mouseRelease(foldersPage, x + 60, 100)
            tryCompare(LibraryFolders, "paneWidth", before + 60)
            LibraryFolders.paneWidth = 320
        }
    }
}
