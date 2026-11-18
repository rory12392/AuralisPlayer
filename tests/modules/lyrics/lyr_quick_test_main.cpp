// 歌詞模組的 Qt Quick Test（qml/tst_*.qml，T-66）：歌詞面板（S-12）的狀態、目前行置中、
// 手動捲動後暫停、點行與鍵盤跳轉；正在播放面板的歌詞預覽（S-11）。
// 面板用到 Auralis.Ui 的共用元件與核心的介面服務（Navigator），所以和其他模組的 Quick Test 一樣
// 先放好 ThemeController 與 CoreServices；歌詞由真的載入器在背景找（記憶體裡的音樂庫給內嵌歌詞），
// 播放是假的 PlaybackService（QML 以 fakePlayback 取用）。用 offscreen 平台跑，不需要螢幕。
#include "backdrop.h"
#include "fake_outputs.h"
#include "fake_playback_service.h"
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "lrclib.h"
#include "lyrics_loader.h"
#include "lyrics_view.h"
#include "modules/core_services.h"
#include "shell/core_bindings.h"
#include "shell/output_controller.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "theme/theme_controller.h"

#include <auralis/cover_service.h>
#include <auralis/module_settings.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QThread>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include <atomic>
#include <memory>
#include <mutex>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)
Q_IMPORT_QML_PLUGIN(Auralis_LyrPlugin)

namespace {

using namespace auralis;

/// 記憶體裡的音樂庫：只提供內嵌歌詞（依路徑），可以讓讀取變慢（看載入中的狀態）。
class QuickLibrary final : public LibraryQuery {
public:
    void set(const QString& path, const QString& lyrics, int delayMs) {
        std::lock_guard lock(m_mutex);
        m_lyrics.insert(path, lyrics);
        m_delay.insert(path, delayMs);
    }
    /// 這首的封面 key（全螢幕底色，T-69）；空字串＝沒有封面。
    void setCover(const QString& path, const QString& coverKey) {
        std::lock_guard lock(m_mutex);
        m_cover.insert(path, coverKey);
    }
    Result<std::optional<TrackInfo>> findByIdentity(const QString& path, quint32) const override {
        std::lock_guard lock(m_mutex);
        if (!m_cover.contains(path)) {
            return std::optional<TrackInfo>();
        }
        TrackInfo info;
        info.ref = {path, 0};
        info.coverKey = m_cover.value(path);
        return std::optional<TrackInfo>(info);
    }
    Result<QString> audioFilePath(const TrackRef& ref) const override { return ref.path; }
    Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
        return QList<TrackInfo>();
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef& track) const override {
        int delay = 0;
        QString lyrics;
        {
            std::lock_guard lock(m_mutex);
            delay = m_delay.value(track.path);
            lyrics = m_lyrics.value(track.path);
        }
        QThread::msleep(static_cast<unsigned long>(delay));
        return lyrics;
    }

private:
    mutable std::mutex m_mutex;
    QHash<QString, QString> m_lyrics;
    QHash<QString, int> m_delay;
    QHash<QString, QString> m_cover;
};

/// 封面服務：key 就是主色的十六進位（"ff0000"），讀不到的 key 回傳 nullopt（T-69）。
class QuickCovers final : public CoverService {
public:
    std::optional<quint32> dominantColor(const QString& key) override {
        bool ok = false;
        const quint32 rgb = key.toUInt(&ok, 16);
        return ok ? std::optional<quint32>(rgb) : std::nullopt;
    }
    Result<QString> exportThumbnail(const QString&, int) override {
        return Error{QStringLiteral("不支援"), 0, QStringLiteral("exportThumbnail")};
    }
};

class MemorySettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& fallback) const override {
        return m_values.value(key, fallback);
    }
    void setValue(const QString& key, const QVariant& value) override {
        m_values.insert(key, value);
    }
    bool contains(const QString& key) const override { return m_values.contains(key); }
    void remove(const QString& key) override { m_values.remove(key); }

private:
    QHash<QString, QVariant> m_values;
};

/// 代替 LRCLIB 的最後一段（T-67；Quick Test 不連線）：結果由 QML 經 lyricsProbe.setOnlineResult
/// 指定（""＝沒找到、"instrumental"、"unavailable"、"synced"）；設定關閉時跳過，同 LrclibStep。
class OnlineStub final : public mod::lyr::LyricsStep {
public:
    explicit OnlineStub(std::shared_ptr<const mod::lyr::LrclibSwitches> switches)
        : m_switches(std::move(switches)) {}
    void setResult(const QString& result) {
        std::lock_guard lock(m_mutex);
        m_result = result;
    }
    mod::lyr::LyricsSource kind() const override { return mod::lyr::LyricsSource::Lrclib; }
    bool active() const override { return m_switches->online.load(); }
    std::optional<mod::lyr::LyricsDocument> find(const mod::lyr::LyricsTrack&) override {
        QString result;
        {
            std::lock_guard lock(m_mutex);
            result = m_result;
        }
        using mod::lyr::LyricsDocument;
        LyricsDocument doc;
        doc.source = mod::lyr::LyricsSource::Lrclib;
        if (result == QStringLiteral("instrumental")) {
            doc.status = LyricsDocument::Status::Found;
            doc.instrumental = true;
        } else if (result == QStringLiteral("unavailable")) {
            doc.status = LyricsDocument::Status::Unavailable;
        } else if (result == QStringLiteral("synced")) {
            doc =
                mod::lyr::parseLyrics(QStringLiteral("[00:01.00]線上第一行\n[00:02.00]線上第二行"));
            doc.source = mod::lyr::LyricsSource::Lrclib;
        } else {
            return std::nullopt;
        }
        return doc;
    }

private:
    std::shared_ptr<const mod::lyr::LrclibSwitches> m_switches;
    std::mutex m_mutex;
    QString m_result;
};

/// 讓 QML 測試換歌：每次用新的檔名（快取以路徑為鍵），歌詞放進記憶體音樂庫再交給載入器。
class LyricsProbe : public QObject {
    Q_OBJECT

public:
    LyricsProbe(QString dir, QuickLibrary& library, mod::lyr::LyricsLoader& loader,
                MemorySettings& settings, OnlineStub& online, mod::lyr::BackdropLoader& backdrop)
        : m_dir(std::move(dir)), m_library(library), m_loader(loader), m_settings(settings),
          m_online(online), m_backdrop(backdrop) {}
    /// lyrics 空字串＝沒有歌詞；delayMs 讓讀取變慢。
    Q_INVOKABLE void load(const QString& lyrics, int delayMs = 0) {
        const QString path = m_dir + QStringLiteral("/t%1.flac").arg(++m_count);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write("audio");
        }
        file.close();
        m_library.set(path, lyrics, delayMs);
        m_library.setCover(path, m_nextCover);
        m_loader.load({path, 0});
        m_backdrop.load({path, 0}); // 同模組：換歌時歌詞與底色一起載入
    }
    /// 之後載入的曲目的封面主色（"ff0000"；空字串＝沒有封面，T-69）。
    Q_INVOKABLE void setNextCover(const QString& rgbHex) { m_nextCover = rgbHex; }
    Q_INVOKABLE void setOnline(bool on) { m_settings.setValue(QStringLiteral("online"), on); }
    /// 之後載入的曲目本機沒有歌詞時，線上那段回傳什麼（T-67）。
    Q_INVOKABLE void setOnlineResult(const QString& result) { m_online.setResult(result); }

private:
    QString m_dir;
    QuickLibrary& m_library;
    mod::lyr::LyricsLoader& m_loader;
    MemorySettings& m_settings;
    OnlineStub& m_online;
    mod::lyr::BackdropLoader& m_backdrop;
    QString m_nextCover;
    int m_count = 0;
};

} // namespace

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        ui::loadBundledFonts();
        const QString root = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-66-quick");
        // 每個 tst_*.qml 各開一次引擎：只在第一次清掉上一輪，否則前一個檔案的截圖會被刪
        static bool cleaned = false;
        if (!cleaned) {
            QDir(root).removeRecursively();
            cleaned = true;
        }
        QDir().mkpath(root);
        const QString dbPath = root + QStringLiteral("/library.db");
        m_database.reset();
        QFile::remove(dbPath); // 快取不帶到下一個檔案（曲目檔名每個檔案從 t1 重新編）
        if (!core::migrateDatabase(dbPath).hasValue()) {
            qFatal("測試資料庫升級失敗");
        }
        m_database = std::make_unique<core::ModuleDatabaseImpl>(QStringLiteral("LYR"), dbPath);
        m_theme = std::make_unique<ui::ThemeController>(root + QStringLiteral("/settings.ini"),
                                                        nullptr, [] { return true; });
        ui::ThemeController::setInstance(m_theme.get());
        m_backdrop.reset(); // 參照上一輪的 JobRunner，先刪
        m_services = std::make_unique<core::CoreServices>();
        ui::bindCoreServices(*m_services);
        // 預覽點一下要切到這個頁籤：登記了 showPanel 才會發出 panelRequested
        m_services->ui.addPanel({QStringLiteral("panel/lyrics"), QStringLiteral("歌詞"),
                                 QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Lyr/LyricsPanel.qml")),
                                 20});
        m_loader =
            std::make_unique<mod::lyr::LyricsLoader>(m_services->jobs, m_library, *m_database);
        auto online = std::make_unique<OnlineStub>(m_switches);
        OnlineStub& stub = *online;
        m_loader->appendSource(std::move(online));
        m_view = std::make_unique<mod::lyr::LyricsView>(*m_loader, m_fake, &m_settings, m_switches);
        mod::lyr::LyricsView::setInstance(m_view.get());
        // 全螢幕底色（T-69）：真的 BackdropLoader，封面主色由假的封面服務給
        m_backdrop =
            std::make_unique<mod::lyr::BackdropLoader>(m_services->jobs, m_library, m_covers);
        m_view->setBackdrop(m_backdrop.get());
        m_probe = std::make_unique<LyricsProbe>(root, m_library, *m_loader, m_settings, stub,
                                                *m_backdrop);
        // 全螢幕（T-68）讀 Player 與 Output：曲名「全螢幕曲名 <n>」（n 是假佇列的檔名）、
        // 藝人、專輯與格式固定；輸出清單用介面測試的假輸出
        m_player.reset();
        m_output.reset();
        m_queue = std::make_unique<ui::QueueModel>(
            m_fake, nullptr, [](const TrackRef& track) -> std::optional<ui::QueueTrackMeta> {
                ui::QueueTrackMeta meta;
                meta.title = QStringLiteral("全螢幕曲名 ") + QFileInfo(track.path).baseName();
                meta.artist = QStringLiteral("全螢幕藝人");
                meta.durationMs = 200000;
                return meta;
            });
        m_player = std::make_unique<ui::PlayerController>(
            m_fake, *m_queue, [](const TrackRef&) -> std::optional<ui::NowPlayingInfo> {
                ui::NowPlayingInfo info;
                info.album = QStringLiteral("全螢幕專輯");
                info.codec = QStringLiteral("FLAC");
                info.sampleRate = 96000;
                info.bits = 24;
                return info;
            });
        ui::PlayerController::setInstance(m_player.get());
        m_output = std::make_unique<ui::OutputController>(m_fake, nullptr, m_outputs.hooks());
        ui::OutputController::setInstance(m_output.get());
        engine->rootContext()->setContextProperty(QStringLiteral("fakePlayback"), &m_fake);
        engine->rootContext()->setContextProperty(QStringLiteral("lyricsProbe"), m_probe.get());
        // 面板截圖（主程式的 --screenshot 還不能切到歌詞頁籤）存在測試暫存，收尾時挑進證據
        engine->rootContext()->setContextProperty(QStringLiteral("shotDir"), root);
    }

private:
    QuickLibrary m_library;
    MemorySettings m_settings;
    std::shared_ptr<mod::lyr::LrclibSwitches> m_switches =
        std::make_shared<mod::lyr::LrclibSwitches>();
    test::FakePlaybackService m_fake;
    std::unique_ptr<core::ModuleDatabaseImpl> m_database;
    std::unique_ptr<ui::ThemeController> m_theme;
    std::unique_ptr<core::CoreServices> m_services;
    std::unique_ptr<mod::lyr::LyricsLoader> m_loader;
    std::unique_ptr<mod::lyr::LyricsView> m_view;
    QuickCovers m_covers;
    std::unique_ptr<mod::lyr::BackdropLoader> m_backdrop;
    std::unique_ptr<LyricsProbe> m_probe;
    test::FakeOutputs m_outputs;
    std::unique_ptr<ui::QueueModel> m_queue;
    std::unique_ptr<ui::PlayerController> m_player;
    std::unique_ptr<ui::OutputController> m_output;
};

QUICK_TEST_MAIN_WITH_SETUP(lyr_panels, Setup)

#include "lyr_quick_test_main.moc"
