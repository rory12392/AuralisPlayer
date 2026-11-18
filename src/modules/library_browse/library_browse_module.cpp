// 音樂庫瀏覽與搜尋模組的進入點：搜尋與篩選的服務（單例 LibrarySearch，T-20）、
// 側欄頁「所有曲目」與它的播放動作（單例 LibraryPlayback）、工具列的搜尋框（T-21）、
// 側欄頁「專輯」（單例 LibraryAlbums，T-22）、「藝人」「資料夾」（單例 LibraryArtists、
// LibraryFolders，T-23）、不在側欄的專輯詳細頁（單例 LibraryAlbum，T-24）。
#include "album_detail_controller.h"
#include "albums_controller.h"
#include "artists_controller.h"
#include "folders_controller.h"
#include "library_playback.h"
#include "search_controller.h"
#include "track_columns_setting.h"

#include <auralis/module.h>
#include <auralis/module_settings.h>
#include <auralis/ui_registry.h>

#include <QCoreApplication>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

// 頁面用的 QML 模組 Auralis.Lib（靜態外掛）
Q_IMPORT_QML_PLUGIN(Auralis_LibPlugin)

namespace auralis::mod::lib {

namespace {

class LibraryBrowseModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("LIB"), QStringLiteral("音樂庫瀏覽與搜尋"), QStringLiteral("1")};
    }
    void start(ModuleHost& host) override {
        m_search = std::make_unique<SearchController>(host.library());
        SearchController::setInstance(m_search.get());
        // 曲目清單的欄位與欄寬（T-90，所有曲目與資料夾頁共用，存 LIB/trackColumns）
        m_columns = std::make_unique<TrackColumnsSetting>(&host.settings());
        TrackColumnsSetting::setInstance(m_columns.get());
        m_playback = std::make_unique<LibraryPlayback>(host.playback(), *m_search);
        LibraryPlayback::setInstance(m_playback.get());
        m_albums = std::make_unique<AlbumsController>(host.library(), host.playback(), *m_search,
                                                      &host.settings());
        AlbumsController::setInstance(m_albums.get());
        // 專輯詳細頁（T-24）：不在側欄，由專輯頁、藝人頁以 Navigator.open("library/album",
        // {albumId}) 開；用專輯頁讀好的專輯，不另外讀音樂庫
        m_albumDetail = std::make_unique<AlbumDetailController>(*m_albums, host.playback());
        AlbumDetailController::setInstance(m_albumDetail.get());
        host.ui().addPage({QStringLiteral("library/album"),
                           QCoreApplication::translate("LibraryBrowseModule", "專輯詳細"),
                           QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/AlbumPage.qml")), 0});
        // 專輯頁（T-22）：側欄排在「所有曲目」前面（專輯 10、藝人 20、資料夾 30）
        host.ui().addSidebarPage({QStringLiteral("library/albums"),
                                  QCoreApplication::translate("LibraryBrowseModule", "專輯"),
                                  QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/AlbumsPage.qml")),
                                  10});
        // 藝人頁（T-23）：用專輯頁讀好的專輯分組，不另外讀音樂庫
        m_artists = std::make_unique<ArtistsController>(*m_albums);
        ArtistsController::setInstance(m_artists.get());
        host.ui().addSidebarPage({QStringLiteral("library/artists"),
                                  QCoreApplication::translate("LibraryBrowseModule", "藝人"),
                                  QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/ArtistsPage.qml")),
                                  20});
        // 資料夾頁（T-23）
        m_folders =
            std::make_unique<FoldersController>(host.library(), host.playback(), &host.settings());
        FoldersController::setInstance(m_folders.get());
        host.ui().addSidebarPage({QStringLiteral("library/folders"),
                                  QCoreApplication::translate("LibraryBrowseModule", "資料夾"),
                                  QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/FoldersPage.qml")),
                                  30});
        host.ui().addSidebarPage({QStringLiteral("library/tracks"),
                                  QCoreApplication::translate("LibraryBrowseModule", "所有曲目"),
                                  QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/TracksPage.qml")),
                                  40});
        // 工具列的搜尋框（每一頁都看得到；T-28 的工具列常駐項目）
        host.ui().addToolbarItem(
            {QStringLiteral("library/search"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/LibrarySearchBox.qml")), 10});
        // 曲目右鍵選單的「前往專輯」「前往藝人」（T-25；排在 OUT 的「計算 ReplayGain」90 前面）
        host.ui().addTrackMenuSection(
            {QStringLiteral("library/goto"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/LibraryTrackMenuSection.qml")), 50});
    }
    void stop() noexcept override {
        // 程式關閉：丟掉排隊中的查詢、等正在查的那一批結束
        if (m_folders) {
            m_folders->shutdown();
        }
        if (m_albums) {
            m_albums->shutdown();
        }
        if (m_search) {
            m_search->shutdown();
        }
    }

private:
    /// 留到模組物件解構（QML 的單例參照它，stop() 時介面可能還在）。
    std::unique_ptr<SearchController> m_search;
    std::unique_ptr<TrackColumnsSetting> m_columns;
    std::unique_ptr<LibraryPlayback> m_playback;
    std::unique_ptr<AlbumsController> m_albums;
    std::unique_ptr<AlbumDetailController> m_albumDetail;
    std::unique_ptr<ArtistsController> m_artists;
    std::unique_ptr<FoldersController> m_folders;
};

} // namespace
} // namespace auralis::mod::lib

AURALIS_REGISTER_MODULE(LIB, [] {
    return std::unique_ptr<auralis::IModule>(
        std::make_unique<auralis::mod::lib::LibraryBrowseModule>());
})
