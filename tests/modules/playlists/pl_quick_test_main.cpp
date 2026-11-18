// 播放清單模組的 Qt Quick Test（qml/tst_*.qml，T-50）：側欄播放清單區的
// 新增、改名、刪除（鍵盤與右鍵）、拖到某一列加入、拖到空位建立新清單，
// 以及曲目右鍵選單的「加入播放清單 ▸」。
// 播放清單頁（T-55）：版面、鍵盤、拖曳排序、移除與復原、接收拖放。
// 側欄用到 Auralis.Ui 的共用元件與核心的介面服務（Navigator、Dialogs），所以和介面測試一樣先放好
// ThemeController 與 CoreServices；播放清單存在測試暫存的真資料庫。
// 用 offscreen 平台跑（ctest 設定）。
#include "fake_playback_service.h"
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "modules/core_services.h"
#include "playlist_exporter.h"
#include "playlist_import.h"
#include "playlist_store.h"
#include "playlist_tracks.h"
#include "playlists_controller.h"
#include "shell/core_bindings.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "theme/theme_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include <memory>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)
Q_IMPORT_QML_PLUGIN(Auralis_PlPlugin)

namespace {

using namespace auralis;

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        ui::loadBundledFonts();
        const QString root = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-50-quick");
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        m_theme = std::make_unique<ui::ThemeController>(root + QStringLiteral("/settings.ini"),
                                                        nullptr, [] { return true; });
        ui::ThemeController::setInstance(m_theme.get());
        m_services = std::make_unique<core::CoreServices>();
        ui::bindCoreServices(*m_services);

        const QString db = root + QStringLiteral("/library.db");
        core::migrateDatabase(db);
        m_db = std::make_unique<core::ModuleDatabaseImpl>(QStringLiteral("PL"), db);
        m_store = std::make_unique<mod::pl::PlaylistStore>(*m_db);
        // 播放用假的播放服務（不開檔），音樂庫不查（曲目都標找不到檔案，曲名用檔名）。
        // 每個測試檔各一個引擎：先拆掉上一個檔案留下的、參照別的物件的那幾個
        m_player.reset();
        m_queue.reset();
        m_importer.reset();
        m_exporter.reset();
        m_tracks.reset();
        m_playlists = std::make_unique<mod::pl::PlaylistsController>(*m_store, m_services->dialogs,
                                                                     m_services->notifier, nullptr,
                                                                     nullptr, nullptr, &m_playback);
        mod::pl::PlaylistsController::setInstance(m_playlists.get());
        m_tracks =
            std::make_unique<mod::pl::PlaylistTracks>(*m_store, *m_playlists, nullptr, &m_playback);
        mod::pl::PlaylistTracks::setInstance(m_tracks.get());
        // 匯入（T-51）：沒給 JobRunner，在 UI 執行緒直接做；開檔對話框照樣經 coreDialogs
        m_importer = std::make_unique<mod::pl::PlaylistImporter>(
            *m_store, *m_playlists, m_services->dialogs, m_services->notifier, nullptr,
            &m_services->navigator);
        mod::pl::PlaylistImporter::setInstance(m_importer.get());
        // 匯出（T-54）：沒給設定、音樂庫與 JobRunner（不記模式、曲目都當找不到、在 UI 執行緒寫）
        m_exporter = std::make_unique<mod::pl::PlaylistExporter>(
            *m_store, m_services->dialogs, m_services->notifier, nullptr, nullptr, nullptr);
        mod::pl::PlaylistExporter::setInstance(m_exporter.get());
        engine->rootContext()->setContextProperty(QStringLiteral("plPlayback"), &m_playback);
        // 頁面的清單用核心的 Player（Shift+Enter、Ctrl+Enter、右鍵選單），接同一個假的播放服務
        m_queue = std::make_unique<ui::QueueModel>(m_playback, &m_services->notifier,
                                                   ui::QueueMetaLookup{});
        m_player = std::make_unique<ui::PlayerController>(m_playback, *m_queue);
        ui::PlayerController::setInstance(m_player.get());
        // 照模組 start() 的登記：播放清單頁與右鍵區塊
        m_services->ui.addPage({QStringLiteral("playlist"), QStringLiteral("播放清單"),
                                QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Pl/PlaylistPage.qml")),
                                0});
        m_services->ui.addTrackMenuSection(
            {QStringLiteral("playlists/add"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Pl/PlaylistTrackMenuSection.qml")), 10});
        // 確認對話框：測試從 coreDialogs 的 requested 訊號拿到請求，用 respond 回覆
        engine->rootContext()->setContextProperty(QStringLiteral("coreDialogs"),
                                                  &m_services->dialogs);
        engine->rootContext()->setContextProperty(QStringLiteral("coreNotifier"),
                                                  &m_services->notifier);
        engine->rootContext()->setContextProperty(QStringLiteral("plProbe"), this);
    }

public:
    /// 每個測試開始前清空播放清單。
    Q_INVOKABLE void resetPlaylists() {
        const auto list = m_store->list();
        if (list) {
            for (const auto& info : list.value()) {
                m_store->remove(info.id);
            }
        }
        m_playlists->setEditingId(0);
        m_playlists->reload();
        m_playback.reset(0, -1);
    }

    /// 曲目用的磁碟機代號：和測試暫存不同的磁碟機（匯出的相對路徑測試要「不同磁碟機寫完整路徑」；
    /// CI 的工作區在 D:，原本寫死 D: 時曲目和匯出檔同一個磁碟機）。
    Q_INVOKABLE QString trackDrive() const {
        return QString::fromUtf8(AURALIS_TEST_TMP).startsWith(QLatin1Char('Q'), Qt::CaseInsensitive)
                   ? QStringLiteral("R:")
                   : QStringLiteral("Q:");
    }

    /// 建立名為 name 的播放清單，依序放 names 這幾首（<trackDrive>/音樂/<名稱>.flac）；回傳 id。
    Q_INVOKABLE qint64 makePlaylist(const QString& name, const QStringList& names) {
        const auto id = m_store->create(name);
        QList<TrackRef> tracks;
        for (const auto& track : names) {
            tracks.append({trackDrive() + QStringLiteral("/音樂/%1.flac").arg(track), 0});
        }
        m_store->addTracks(id.value(), tracks);
        m_playlists->reload();
        return id.value();
    }

    /// 測試暫存裡的路徑（截圖用，T-51 的 S-09）。不放 t-50-quick：下一個測試檔開始時會清掉它。
    Q_INVOKABLE QString tempPath(const QString& name) const {
        const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-51-shots");
        QDir().mkpath(dir);
        return dir + QLatin1Char('/') + name;
    }

    /// 在測試暫存寫一個 UTF-8 的播放清單檔（T-51），回傳完整路徑。
    Q_INVOKABLE QString writePlaylist(const QString& name, const QString& text) const {
        const QString path =
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-50-quick/") + name;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(text.toUtf8());
        return QDir::toNativeSeparators(path);
    }

    /// 在測試暫存建一個檔（T-52 重新對應用；relative 可以有子資料夾），回傳完整路徑（Windows
    /// 分隔符）。
    Q_INVOKABLE QString makeFile(const QString& relative) const {
        const QString path =
            QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-50-quick/") + relative;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write("x");
        return QDir::toNativeSeparators(path);
    }
    /// 直接在資料庫加一首、不通知側欄（F-1：模擬掃描後整張 CUE 展開，項目數變了）。
    Q_INVOKABLE void addTrackBehind(qint64 id, const QString& path) {
        m_store->addTracks(id, {{path, 0}});
    }
    /// 掃描完成後的重新檢查（模組在 LibraryManager 掃描完成時呼叫的那一個）。
    Q_INVOKABLE void recheckAll() { m_importer->recheckAll(); }
    /// 測試暫存資料夾（Windows 分隔符）。
    Q_INVOKABLE QString quickDir() const {
        return QDir::toNativeSeparators(QString::fromUtf8(AURALIS_TEST_TMP) +
                                        QStringLiteral("/t-50-quick"));
    }
    /// T-52 的截圖放這裡（同 tempPath，另一個資料夾）。
    Q_INVOKABLE QString shotPath(const QString& name) const {
        const QString dir = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-52-shots");
        QDir().mkpath(dir);
        return dir + QLatin1Char('/') + name;
    }

    /// 檔案內容（UTF-8，T-54 匯出用）；讀不到時 "<無>"。
    Q_INVOKABLE QString readText(const QString& path) const {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QStringLiteral("<無>");
        }
        return QString::fromUtf8(file.readAll());
    }

    /// 資料庫裡的順序（檔名，不含副檔名）。
    Q_INVOKABLE QStringList storedNames(qint64 id) const {
        QStringList names;
        const auto items = m_store->items(id);
        for (const auto& item : items.value()) {
            names.append(
                item.track.path.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0));
        }
        return names;
    }

private:
    test::FakePlaybackService m_playback;
    std::unique_ptr<ui::ThemeController> m_theme;
    std::unique_ptr<core::CoreServices> m_services;
    std::unique_ptr<core::ModuleDatabaseImpl> m_db;
    std::unique_ptr<mod::pl::PlaylistStore> m_store;
    std::unique_ptr<mod::pl::PlaylistsController> m_playlists;
    std::unique_ptr<mod::pl::PlaylistTracks> m_tracks;
    std::unique_ptr<mod::pl::PlaylistImporter> m_importer;
    std::unique_ptr<mod::pl::PlaylistExporter> m_exporter;
    std::unique_ptr<ui::QueueModel> m_queue;
    std::unique_ptr<ui::PlayerController> m_player;
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(pl_sidebar, Setup)

#include "pl_quick_test_main.moc"
