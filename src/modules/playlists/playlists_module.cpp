// 播放清單模組的進入點（T-50）：播放清單儲存（PlaylistStore）與單例 Playlists、
// 側欄的播放清單區、不在側欄的播放清單頁（playlist，參數 {id}）、曲目右鍵選單的「加入播放清單 ▸」、
// 匯入 m3u／m3u8／pls（T-51，單例 PlaylistImport，接手拖進視窗與命令列的播放清單檔）、
// 找不到的檔案與重新對應（T-52）、匯出 .m3u8（T-54，單例 PlaylistExport）。
#include "playlist_exporter.h"
#include "playlist_import.h"
#include "playlist_store.h"
#include "playlist_tracks.h"
#include "playlists_controller.h"

#include <auralis/module.h>
#include <auralis/ui_registry.h>

#include <QCoreApplication>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

// 側欄區塊與頁面用的 QML 模組 Auralis.Pl（靜態外掛）
Q_IMPORT_QML_PLUGIN(Auralis_PlPlugin)

namespace auralis::mod::pl {

namespace {

class PlaylistsModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("PL"), QStringLiteral("播放清單"), QStringLiteral("1")};
    }
    void start(ModuleHost& host) override {
        m_store = std::make_unique<PlaylistStore>(host.database());
        // 只帶查詢條件（超過 5,000 首）的拖放與右鍵：在背景工作裡向音樂庫依條件取曲目
        LibraryQuery& library = host.library();
        m_controller = std::make_unique<PlaylistsController>(
            *m_store, host.dialogs(), host.notifier(),
            [&library](const QString& query) { return library.tracksForDragQuery(query); },
            &host.jobs(), nullptr, &host.playback());
        PlaylistsController::setInstance(m_controller.get());
        // 播放清單頁的曲目清單（T-55）
        m_tracks =
            std::make_unique<PlaylistTracks>(*m_store, *m_controller, &library, &host.playback());
        PlaylistTracks::setInstance(m_tracks.get());
        // 匯入播放清單（T-51）：側欄與空狀態的按鈕，以及拖進視窗、命令列的播放清單檔（T-61 分流）
        m_importer =
            std::make_unique<PlaylistImporter>(*m_store, *m_controller, host.dialogs(),
                                               host.notifier(), &host.jobs(), &host.navigator());
        PlaylistImporter::setInstance(m_importer.get());
        // 匯出 .m3u8（T-54）：路徑模式記在 PL/ 段，讀項目與寫檔在背景工作
        m_exporter = std::make_unique<PlaylistExporter>(*m_store, host.dialogs(), host.notifier(),
                                                        &host.settings(), &library, &host.jobs());
        PlaylistExporter::setInstance(m_exporter.get());
        // .fpl 內嵌 cue 的項目向音樂庫找 CUE 曲目（T-53）
        m_importer->setCueLookup([&library] { return libraryCueLookup(library); });
        // m3u／pls 的整張 CUE 展開、重新檢查整張 CUE 與內嵌 cue 時向音樂庫查（T-56）
        m_importer->setLibraryCheck([&library] { return libraryCheck(library); });
        // 音樂庫掃描完成後重新檢查所有播放清單的找不到的檔案（T-52，§1 情況 2；在工作執行緒）
        QObject::connect(&library, &LibraryQuery::scanFinished, m_importer.get(),
                         [importer = m_importer.get()] { importer->recheckAll(); });
        host.ui().addFileOpenHandler({QStringLiteral("m3u"), QStringLiteral("m3u8"),
                                      QStringLiteral("pls"), QStringLiteral("fpl")},
                                     [importer = m_importer.get()](const QStringList& paths) {
                                         importer->importFiles(paths);
                                     });
        host.ui().addSidebarSection(
            {QStringLiteral("playlists"),
             QCoreApplication::translate("PlaylistsModule", "播放清單"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Pl/PlaylistsSection.qml")), 10});
        host.ui().addPage({QStringLiteral("playlist"),
                           QCoreApplication::translate("PlaylistsModule", "播放清單"),
                           QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Pl/PlaylistPage.qml")), 0});
        // 排在 LIB 的「前往專輯／藝人」50 與 OUT 的「計算 ReplayGain」90 前面：加入播放清單比較常用
        host.ui().addTrackMenuSection(
            {QStringLiteral("playlists/add"), QString(),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Pl/PlaylistTrackMenuSection.qml")), 10});
    }
    void stop() noexcept override {
        // 核心在模組停止後先解構音樂庫與資料庫，最後才是 JobRunner；播放清單儲存也隨模組刪掉。
        // 所以這裡等自己的背景工作結束，之後才開始的直接結束（P-PL-24）
        if (m_controller) {
            m_controller->shutdown(kStopTimeoutMs);
        }
        if (m_importer) {
            m_importer->shutdown(kStopTimeoutMs);
        }
        if (m_exporter) {
            m_exporter->shutdown(kStopTimeoutMs);
        }
    }

private:
    /// 停止時等背景工作的上限（和其他模組相同）；逾時寫日誌、照常停止。
    static constexpr int kStopTimeoutMs = 7000;
    /// 留到模組物件解構（QML 的單例參照它，stop() 時介面可能還在）。
    std::unique_ptr<PlaylistStore> m_store;
    std::unique_ptr<PlaylistsController> m_controller;
    std::unique_ptr<PlaylistTracks> m_tracks;
    std::unique_ptr<PlaylistImporter> m_importer;
    std::unique_ptr<PlaylistExporter> m_exporter;
};

} // namespace
} // namespace auralis::mod::pl

AURALIS_REGISTER_MODULE(PL, [] {
    return std::unique_ptr<auralis::IModule>(std::make_unique<auralis::mod::pl::PlaylistsModule>());
})
