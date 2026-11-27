// 音樂庫瀏覽模組的 Qt Quick Test（qml/tst_*.qml）：專輯頁（S-03，T-22）的鍵盤操作與三種檢視；
// 藝人頁、資料夾頁（S-05、S-06，T-23）的鍵盤操作；專輯詳細頁（S-04，T-24）；
// 各清單的選取、右鍵選單與拖放（T-25）；曲目清單的位元率、加入時間兩欄與排序（T-115）。
// 頁面用到 Auralis.Ui 的共用元件與核心的介面服務（Navigator、Library），所以和介面測試一樣
// 先放好 ThemeController 與 CoreServices；LibraryAlbums、LibrarySearch 背後是記憶體裡的音樂庫，
// 播放是假的 PlaybackService（QML 以 fakePlayback 取用）。用 offscreen 平台跑（ctest 設定），
// 不需要螢幕、不搶前景。
#include "album_detail_controller.h"
#include "albums_controller.h"
#include "artists_controller.h"
#include "fake_playback_service.h"
#include "folders_controller.h"
#include "library_playback.h"
#include "modules/core_services.h"
#include "search_controller.h"
#include "shell/core_bindings.h"
#include "shell/library_controller.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "theme/theme_controller.h"
#include "track_columns_setting.h"

#include <QDateTime>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)
Q_IMPORT_QML_PLUGIN(Auralis_LibPlugin)

namespace {

using namespace auralis;

TrackInfo makeTrack(qint64 albumId, int no, const QString& codec, int rate, int bits) {
    TrackInfo t;
    t.albumId = albumId;
    t.folderId = albumId <= 4 ? 3 : 4; // 資料夾頁（T-23）：專輯 1–4 在 ACG\Anime、5–8 在「其他」
    t.ref = {QStringLiteral("C:/t/%1-%2.flac").arg(albumId).arg(no), 0};
    t.title = QStringLiteral("曲目 %1-%2").arg(albumId).arg(no);
    t.artist = QStringLiteral("藝人 %1").arg(albumId);
    t.albumArtist = t.artist;
    t.trackNo = no;
    t.durationMs = 180'000;
    t.codec = codec;
    t.sampleRate = rate;
    t.bits = bits;
    // T-115：專輯 2 沒有位元率、專輯 5 沒有加入時間（補值中）；有值的位元率隨專輯遞減
    if (albumId != 2) {
        t.bitrate = 3000 - static_cast<int>(albumId) * 100 - no;
    }
    if (albumId != 5) {
        t.addedAt =
            QDateTime(QDate(2026, 1, static_cast<int>(albumId)), QTime(12, no)).toMSecsSinceEpoch();
    }
    return t;
}

/// 依位元率或加入時間排序（同 LibraryQuery::tracks：沒有值的不論升降冪都排最後，同值照原順序）。
void sortTracks(QList<TrackInfo>& tracks, const TrackFilter& filter) {
    if (filter.sort == TrackSort::Default) {
        return;
    }
    const auto key = [&filter](const TrackInfo& t) -> std::optional<qint64> {
        if (filter.sort == TrackSort::Bitrate) {
            return t.bitrate ? std::optional<qint64>(*t.bitrate) : std::nullopt;
        }
        return t.addedAt;
    };
    std::stable_sort(tracks.begin(), tracks.end(), [&](const TrackInfo& a, const TrackInfo& b) {
        const auto ka = key(a);
        const auto kb = key(b);
        if (!ka || !kb) {
            return ka.has_value() && !kb.has_value();
        }
        return filter.descending ? *ka > *kb : *ka < *kb;
    });
}

/// 記憶體裡的音樂庫：8 張專輯（第 3 張是 WAV，篩選用），每張 2 首。
class QuickLibrary final : public LibraryQuery {
public:
    QuickLibrary() {
        for (int a = 1; a <= 8; ++a) {
            AlbumInfo album;
            album.id = a;
            album.title = QStringLiteral("專輯 %1").arg(a);
            album.albumArtist = QStringLiteral("藝人 %1").arg(a);
            album.year = 2000 + a;
            album.folderId = a;
            album.trackCount = 2;
            m_albums.append(album);
            const QString codec = a == 3 ? QStringLiteral("WAV") : QStringLiteral("FLAC");
            m_tracks.append(makeTrack(a, 1, codec, 96000, 24));
            m_tracks.append(makeTrack(a, 2, codec, 96000, 24));
        }
    }
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    /// 讓曲目查詢失敗（篩選選項讀取失敗的錯誤狀態）。
    std::atomic<bool> failTracks{false};

    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        if (failTracks) {
            return Error{QStringLiteral("資料庫被鎖住"), 0, QStringLiteral("test")};
        }
        QList<TrackInfo> out;
        for (const TrackInfo& t : m_tracks) {
            if ((filter.codec.isEmpty() || t.codec == filter.codec) &&
                (filter.folderId == 0 || t.folderId == filter.folderId)) {
                out.append(t);
            }
        }
        sortTracks(out, filter);
        const int limit = filter.limit < 0 ? static_cast<int>(out.size()) : filter.limit;
        return out.mid(filter.offset, limit);
    }
    Result<QList<AlbumInfo>> albums() const override { return m_albums; }
    Result<QStringList> artists() const override { return QStringList(); }
    /// 資料夾樹（T-23）：C:\t → ACG → Anime；C:\t → 其他。
    Result<QList<FolderInfo>> folders(qint64 parentId) const override {
        const QList<FolderInfo> all{
            {1, 0, QStringLiteral("C:\\t"), QStringLiteral("t")},
            {2, 1, QStringLiteral("C:\\t\\ACG"), QStringLiteral("ACG")},
            {3, 2, QStringLiteral("C:\\t\\ACG\\Anime"), QStringLiteral("Anime")},
            {4, 1, QStringLiteral("C:\\t\\其他"), QStringLiteral("其他")}};
        QList<FolderInfo> out;
        for (const FolderInfo& f : all) {
            if (f.parentId == parentId) {
                out.append(f);
            }
        }
        return out;
    }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }

private:
    QList<AlbumInfo> m_albums;
    QList<TrackInfo> m_tracks;
};

/// 讓 QML 測試登記專輯詳細頁（T-24 才有；測試「還沒登記時不做事」與「登記後帶 albumId 開頁」）。
class PageProbe : public QObject {
    Q_OBJECT

public:
    PageProbe(core::CoreServices& services, QuickLibrary& library)
        : m_services(services), m_library(library) {}
    /// 之後的曲目查詢（含篩選選項）失敗或恢復。
    Q_INVOKABLE void setTracksFail(bool fail) { m_library.failTracks = fail; }
    Q_INVOKABLE void addAlbumPage() {
        m_services.ui.addPage({QStringLiteral("library/album"), QStringLiteral("專輯詳細"),
                               QUrl(QStringLiteral("qrc:/no-such-page.qml")), 100});
    }
    /// 換成真的專輯詳細頁（T-24，tst_detail_page.qml；同 id 後登記的取代先登記的）。
    Q_INVOKABLE void addRealAlbumPage() {
        m_services.ui.addPage({QStringLiteral("library/album"), QStringLiteral("專輯詳細"),
                               QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/AlbumPage.qml")), 0});
    }

    /// 藝人頁（T-25：開頁參數 {artist}）。
    Q_INVOKABLE void addArtistsPage() {
        m_services.ui.addSidebarPage(
            {QStringLiteral("library/artists"), QStringLiteral("藝人"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/ArtistsPage.qml")), 20});
    }

private:
    core::CoreServices& m_services;
    QuickLibrary& m_library;
};

} // namespace

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        ui::loadBundledFonts();
        const QString settings =
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-22-quick-settings.ini");
        m_theme = std::make_unique<ui::ThemeController>(settings, nullptr, [] { return true; });
        ui::ThemeController::setInstance(m_theme.get());
        m_services = std::make_unique<core::CoreServices>();
        ui::bindCoreServices(*m_services);
        m_libraryUi = std::make_unique<ui::LibraryController>(
            m_services->libraryManager, m_services->library, nullptr, nullptr);
        ui::LibraryController::setInstance(m_libraryUi.get());
        m_search = std::make_unique<mod::lib::SearchController>(m_library);
        mod::lib::SearchController::setInstance(m_search.get());
        m_columns = std::make_unique<mod::lib::TrackColumnsSetting>(nullptr);
        mod::lib::TrackColumnsSetting::setInstance(m_columns.get());
        m_playback = std::make_unique<mod::lib::LibraryPlayback>(m_fake, *m_search);
        mod::lib::LibraryPlayback::setInstance(m_playback.get());
        m_albums =
            std::make_unique<mod::lib::AlbumsController>(m_library, m_fake, *m_search, nullptr);
        mod::lib::AlbumsController::setInstance(m_albums.get());
        m_albumDetail = std::make_unique<mod::lib::AlbumDetailController>(*m_albums, m_fake);
        mod::lib::AlbumDetailController::setInstance(m_albumDetail.get());
        m_artists = std::make_unique<mod::lib::ArtistsController>(*m_albums);
        mod::lib::ArtistsController::setInstance(m_artists.get());
        m_folders =
            std::make_unique<mod::lib::FoldersController>(m_library, m_fake, nullptr, false);
        mod::lib::FoldersController::setInstance(m_folders.get());
        // 清單的 Shift+Enter、Ctrl+Enter 與右鍵選單（T-25）走核心的 Player；選單裡本模組的
        // 「前往專輯」「前往藝人」照模組 start() 的登記
        m_queue =
            std::make_unique<ui::QueueModel>(m_fake, &m_services->notifier, ui::QueueMetaLookup{});
        m_player = std::make_unique<ui::PlayerController>(m_fake, *m_queue);
        ui::PlayerController::setInstance(m_player.get());
        m_services->ui.addTrackMenuSection(
            {QStringLiteral("library/goto"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lib/LibraryTrackMenuSection.qml")), 50});
        m_probe = std::make_unique<PageProbe>(*m_services, m_library);
        engine->rootContext()->setContextProperty(QStringLiteral("fakePlayback"), &m_fake);
        engine->rootContext()->setContextProperty(QStringLiteral("pageProbe"), m_probe.get());
    }
    void cleanupTestCase() {
        m_folders->shutdown();
        m_albums->shutdown();
        m_search->shutdown();
    }

private:
    QuickLibrary m_library;
    test::FakePlaybackService m_fake;
    std::unique_ptr<ui::ThemeController> m_theme;
    std::unique_ptr<core::CoreServices> m_services;
    std::unique_ptr<ui::LibraryController> m_libraryUi;
    std::unique_ptr<mod::lib::SearchController> m_search;
    std::unique_ptr<mod::lib::TrackColumnsSetting> m_columns;
    std::unique_ptr<mod::lib::LibraryPlayback> m_playback;
    std::unique_ptr<mod::lib::AlbumsController> m_albums;
    std::unique_ptr<mod::lib::AlbumDetailController> m_albumDetail;
    std::unique_ptr<mod::lib::ArtistsController> m_artists;
    std::unique_ptr<mod::lib::FoldersController> m_folders;
    std::unique_ptr<ui::QueueModel> m_queue;
    std::unique_ptr<ui::PlayerController> m_player;
    std::unique_ptr<PageProbe> m_probe;
};

QUICK_TEST_MAIN_WITH_SETUP(lib_pages, Setup)

#include "lib_quick_test_main.moc"
