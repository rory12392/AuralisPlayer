import QtQuick
import QtTest
import Auralis.Ui

// 正在播放面板（S-11，AC-10-6 的畫面部分）：沒有目前曲目時是「尚未播放」，有目前曲目時顯示
// 封面、曲名與四格讀數。Player 背後是假的 PlaybackService（fakePlayback），沒有音樂庫查詢，
// 所以讀數是「—」；查詢與文字的規則在 Catch2 的 test_now_playing 驗證。
Item {
    id: root
    width: 340
    height: 800

    Loader {
        id: loader
        anchors.fill: parent
        source: "qrc:/qt/qml/Auralis/Ui/shell/NowPlayingPanel.qml"
    }

    TestCase {
        name: "NowPlaying"
        when: windowShown

        function item(name) {
            const found = findChild(loader.item, name)
            verify(found !== null, name)
            return found
        }

        function test_empty_then_playing() {
            compare(loader.status, Loader.Ready)
            fakePlayback.reset(0, -1)
            verify(!item("nowPlayingContent").visible)
            verify(findChild(loader.item, "nowPlayingCover") !== null)

            fakePlayback.reset(3, 1)
            tryVerify(() => item("nowPlayingContent").visible)
            compare(item("nowPlayingTitle").text, Player.title)
            compare(item("nowPlayingTitle").text, "1")
            // 封面是面板寬扣兩側內距的正方形
            compare(item("nowPlayingCover").width, root.width - 2 * Theme.space6)
            compare(item("nowPlayingCover").height, item("nowPlayingCover").width)
            compare(Player.readouts.length, 4)
            compare(Player.readouts[0].label, "格式")
            compare(Player.readouts[3].label, "位元率")
            compare(Player.readouts[1].value, "—")
            fakePlayback.reset(0, -1)
        }

        // S-11「點藝人或專輯＝前往該頁」（T-27）：音樂庫的頁面沒有登記（LIB 停用）或不在音樂庫的
        // 專輯裡（albumId 0）時不是連結：不能點、滑鼠不變手形、不進 Tab 順序。
        function test_album_and_artist_links() {
            const start = Navigator.currentPage
            fakePlayback.reset(0, -1)
            nowPlayingProbe.setTrack(7, "測試藝人")
            fakePlayback.reset(3, 1)
            tryCompare(Player, "albumId", 7)
            tryCompare(Player, "artist", "測試藝人")
            const album = item("nowPlayingAlbumLink")
            const artist = item("nowPlayingArtistLink")
            // 文字剛出現，等 Column 排好位置（在封面下面）再點
            tryVerify(() => artist.y > 0 && album.y > artist.y)

            // 頁面沒有登記
            for (const link of [album, artist]) {
                verify(!link.linked)
                verify(!link.activeFocusOnTab)
                compare(link.hoverCursor, Qt.ArrowCursor)
            }
            mouseClick(album)
            mouseClick(artist)
            compare(Navigator.currentPage, start)

            nowPlayingProbe.registerLibraryPages()
            tryVerify(() => album.linked)
            verify(artist.linked)
            for (const link of [album, artist]) {
                verify(link.activeFocusOnTab)
                compare(link.hoverCursor, Qt.PointingHandCursor)
                compare(link.Accessible.role, Accessible.Link)
            }
            compare(album.Accessible.name, "前往專輯 測試專輯")
            compare(artist.Accessible.name, "前往藝人 測試藝人")

            // 滑鼠點專輯：專輯詳細頁，參數是數字的 albumId
            mouseClick(album)
            compare(Navigator.currentPage, "library/album")
            compare(Navigator.params.albumId, 7)

            // 鍵盤：Tab 從藝人到專輯；Enter 觸發
            artist.forceActiveFocus()
            keyClick(Qt.Key_Tab)
            verify(album.activeFocus)
            artist.forceActiveFocus()
            keyClick(Qt.Key_Return)
            compare(Navigator.currentPage, "library/artists")
            compare(Navigator.params.artist, "測試藝人")

            // 換到不在音樂庫專輯裡的曲目：兩個都不能點（先回到專輯頁，才看得出點了沒開藝人頁）
            verify(Navigator.back())
            compare(Navigator.currentPage, "library/album")
            nowPlayingProbe.setTrack(0, "測試藝人")
            fakePlayback.reset(0, -1)
            fakePlayback.reset(3, 1)
            tryCompare(Player, "albumId", 0)
            verify(!album.linked)
            verify(!artist.linked)
            verify(!artist.activeFocusOnTab)
            mouseClick(artist)
            compare(Navigator.currentPage, "library/album")

            nowPlayingProbe.setTrack(0, "")
            fakePlayback.reset(0, -1)
        }
    }
}
