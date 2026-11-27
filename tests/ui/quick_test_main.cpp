// Qt Quick Test（tests/ui/qml/tst_*.qml）：共用元件的鍵盤與焦點行為。
// 共用元件用到 Theme，所以在引擎建立後先放好 ThemeController 的實例。
// 播放列與佇列面板的測試（tst_playback）在主視窗殼裡跑：殼用到核心的介面服務與 ShellState，
// Player 背後是假的 PlaybackService（QML 以 fakePlayback 取用）；輸出按鈕與輸出面板（tst_output）的
// Output 背後是同一個假服務加上假的輸出清單（fakeOutputs）。歡迎畫面與設定頁（tst_library）的
// Library 背後是真的 LibraryManager 與測試暫存的資料庫，測試資料夾由 libraryProbe 產生。
#include "fake_outputs.h"
#include "fake_playback_service.h"
#include "fake_plugins.h"
#include "frame_probe.h"
#include "jobs_probe.h"
#include "library/migrator.h"
#include "modules/core_services.h"
#include "shell/core_bindings.h"
#include "shell/file_location.h"
#include "shell/library_controller.h"
#include "shell/output_controller.h"
#include "shell/player_controller.h"
#include "shell/queue_model.h"
#include "shell/shell_state.h"
#include "theme/theme_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QQmlContext>
#include <QQmlEngine>
#include <QThread>
#include <QtPlugin>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include <atomic>
#include <memory>

Q_IMPORT_QML_PLUGIN(Auralis_UiPlugin)

/// 讓 QML 測試讀寫假的輸出清單（fakeOutputs）：選過哪些裝置、指定被佔用的裝置。
class OutputsProbe : public QObject {
    Q_OBJECT

public:
    explicit OutputsProbe(auralis::test::FakeOutputs& outputs) : m_outputs(outputs) {}
    Q_INVOKABLE QStringList selected() const { return m_outputs.selected; }
    Q_INVOKABLE void setBusy(const QString& deviceId) { m_outputs.busy.insert(deviceId); }
    /// 每個測試開始前：沒有被佔用的裝置、還沒選過。
    Q_INVOKABLE void reset() {
        m_outputs.busy.clear();
        m_outputs.selected.clear();
        m_outputs.current = {};
    }

private:
    auralis::test::FakeOutputs& m_outputs;
};

/// 讓 QML 測試產生音樂資料夾（AURALIS_TEST_TMP/t-15-quick/<name>，先清掉上一輪的）。
class LibraryProbe : public QObject {
    Q_OBJECT

public:
    /// 放 wavCount 個 0.1 秒的靜音 WAV；corrupt 為 true 時再放一個損毀的 .flac。回傳資料夾路徑。
    Q_INVOKABLE QString makeFolder(const QString& name, int wavCount, bool corrupt) const {
        const QString dir = QDir::toNativeSeparators(QString::fromUtf8(AURALIS_TEST_TMP) +
                                                     QStringLiteral("/t-15-quick/") + name);
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        for (int i = 0; i < wavCount; ++i) {
            write(dir + QStringLiteral("\\%1.wav").arg(i), silentWav());
        }
        if (corrupt) {
            write(dir + QStringLiteral("\\壞掉.flac"), QByteArray("這不是 FLAC"));
        }
        return dir;
    }

private:
    static void write(const QString& path, const QByteArray& bytes) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
        }
    }
    static QByteArray silentWav() {
        const quint32 dataSize = 4410 * 4;
        QByteArray bytes("RIFF");
        auto le = [&bytes](quint32 value, int size) {
            for (int i = 0; i < size; ++i) {
                bytes.append(static_cast<char>((value >> (8 * i)) & 0xff));
            }
        };
        le(36 + dataSize, 4);
        bytes.append("WAVEfmt ");
        le(16, 4);
        le(1, 2);
        le(2, 2);
        le(44100, 4);
        le(44100 * 4, 4);
        le(4, 2);
        le(16, 2);
        bytes.append("data");
        le(dataSize, 4);
        bytes.append(QByteArray(static_cast<qsizetype>(dataSize), '\0'));
        return bytes;
    }
};

/// 曲目右鍵選單（tst_track_menu）：存在的檔案路徑、「開啟檔案位置」要開檔案總管的路徑（不真的開）。
class TrackMenuProbe : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString existingFile READ existingFile CONSTANT)
    Q_PROPERTY(QStringList revealed READ revealed NOTIFY changed)

public:
    TrackMenuProbe() {
        auralis::ui::FileLocation::setRevealHook([this](const QString& path) {
            m_revealed.append(path);
            emit changed();
            return true;
        });
        // 數「檔案存在嗎」被呼叫幾次：開選單時不能碰檔案系統（離線磁碟會卡住介面）
        auralis::ui::FileLocation::setExistsHook([this](const QString& path) {
            ++m_existsCalls;
            return QFileInfo(path).isFile();
        });
    }
    ~TrackMenuProbe() override {
        auralis::ui::FileLocation::setRevealHook({});
        auralis::ui::FileLocation::setExistsHook({});
    }
    Q_INVOKABLE int existsCalls() const { return m_existsCalls; }
    QString existingFile() const {
        return QStringLiteral(QUICK_TEST_SOURCE_DIR) + QStringLiteral("/TrackMenuItemFixture.qml");
    }
    QStringList revealed() const { return m_revealed; }
    Q_INVOKABLE void clear() {
        m_revealed.clear();
        m_existsCalls = 0;
        emit changed();
    }

signals:
    void changed();

private:
    QStringList m_revealed;
    std::atomic<int> m_existsCalls = 0;
};

/// 正在播放面板點專輯、點藝人跳頁（tst_now_playing，T-27）：目前曲目查到的專輯 id 與藝人
/// （0 與空字串＝查不到，和沒有接音樂庫一樣），以及登記音樂庫的兩個頁面（LIB 停用時沒有登記）。
/// 查詢在工作執行緒，所以加鎖。
class NowPlayingProbe : public QObject {
    Q_OBJECT

public:
    /// 每個測試檔的引擎建立時換成那一份核心服務的登記處。
    auralis::core::UiRegistryImpl* registry = nullptr;

    /// 之後新出現的目前曲目才用新值（同一首不重查）。
    Q_INVOKABLE void setTrack(qint64 albumId, const QString& artist) {
        const QMutexLocker lock(&m_mutex);
        m_albumId = albumId;
        m_artist = artist;
    }
    Q_INVOKABLE void registerLibraryPages() {
        const QUrl page = QUrl::fromLocalFile(QStringLiteral(QUICK_TEST_SOURCE_DIR) +
                                              QStringLiteral("/LibraryPageFixture.qml"));
        registry->addPage({QStringLiteral("library/album"), QStringLiteral("專輯"), page, 0});
        registry->addSidebarPage(
            {QStringLiteral("library/artists"), QStringLiteral("藝人"), page, 20});
    }
    /// 測試中途才登記的介面（模組晚啟動、或只在某些測試有的模組）：kind 是 panel、fullscreen、
    /// nowPlaying；file 是 qrc: 網址或這個資料夾裡的檔名。
    Q_INVOKABLE void addFixture(const QString& kind, const QString& id, const QString& file,
                                int order) {
        const QUrl source = file.startsWith(QStringLiteral("qrc:"))
                                ? QUrl(file)
                                : QUrl::fromLocalFile(QStringLiteral(QUICK_TEST_SOURCE_DIR) +
                                                      QLatin1Char('/') + file);
        const auralis::UiContribution item{id, id, source, order};
        if (kind == QLatin1String("panel")) {
            registry->addPanel(item);
        } else if (kind == QLatin1String("fullscreen")) {
            registry->addFullscreenPage(item);
        } else if (kind == QLatin1String("nowPlaying")) {
            registry->addNowPlayingSection(item);
        }
    }
    std::optional<auralis::ui::NowPlayingInfo> info(const auralis::TrackRef&) const {
        const QMutexLocker lock(&m_mutex);
        if (m_albumId == 0) {
            return std::nullopt;
        }
        auralis::ui::NowPlayingInfo info;
        info.album = QStringLiteral("測試專輯");
        info.albumId = m_albumId;
        return info;
    }
    std::optional<auralis::ui::QueueTrackMeta> meta(const auralis::TrackRef& track) const {
        const QMutexLocker lock(&m_mutex);
        if (m_artist.isEmpty()) {
            return std::nullopt;
        }
        return auralis::ui::QueueTrackMeta{
            QFileInfo(track.path).completeBaseName(), m_artist, -1, {}};
    }

private:
    mutable QMutex m_mutex;
    qint64 m_albumId = 0;
    QString m_artist;
};

class Setup : public QObject {
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        auralis::ui::loadBundledFonts();
        // ctest 每個 QML 檔一個行程、平行執行：設定檔與資料庫放各自的資料夾，才不會互相覆寫或刪掉
        const QString stateDir =
            qEnvironmentVariable("AURALIS_QUICK_STATE_DIR", QString::fromUtf8(AURALIS_TEST_TMP));
        QDir().mkpath(stateDir);
        const QString settings = stateDir + QStringLiteral("/ui-settings.ini");
        m_theme =
            std::make_unique<auralis::ui::ThemeController>(settings, nullptr, [] { return true; });
        auralis::ui::ThemeController::setInstance(m_theme.get());

        m_shell = std::make_unique<auralis::ui::ShellState>(settings);
        m_shell->useFixedSize(1440, 900);
        auralis::ui::ShellState::setInstance(m_shell.get());
        m_services = std::make_unique<auralis::core::CoreServices>();
        m_services->ui.addPanel(
            {QStringLiteral("panel/queue"), QStringLiteral("佇列"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/QueuePanel.qml")), 30});
        auralis::ui::bindCoreServices(*m_services);
        m_nowPlayingProbe.registry = &m_services->ui;
        m_jobsProbe.runner = &m_services->jobs;
        m_queue = std::make_unique<auralis::ui::QueueModel>(
            m_fake, &m_services->notifier,
            [this](const auralis::TrackRef& track) { return m_nowPlayingProbe.meta(track); });
        // 只帶查詢條件的內容（T-62）：「q:<n>」取到 C:/q/0.flac… n 首，其他的回傳錯誤
        m_queue->setDragQueryResolver(
            [](const QString& query) -> auralis::Result<QList<auralis::TrackInfo>> {
                if (!query.startsWith(QStringLiteral("q:"))) {
                    return auralis::Error{QStringLiteral("不是查詢條件")};
                }
                QList<auralis::TrackInfo> tracks(query.mid(2).toInt());
                for (qsizetype i = 0; i < tracks.size(); ++i) {
                    tracks[i].ref = {QStringLiteral("C:/q/%1.flac").arg(i), 0};
                }
                return tracks;
            });
        m_player = std::make_unique<auralis::ui::PlayerController>(
            m_fake, *m_queue,
            [this](const auralis::TrackRef& track) { return m_nowPlayingProbe.info(track); });
        auralis::ui::PlayerController::setInstance(m_player.get());
        m_output = std::make_unique<auralis::ui::OutputController>(m_fake, &m_services->notifier,
                                                                   m_outputs.hooks());
        auralis::ui::OutputController::setInstance(m_output.get());
        // 音樂庫：每次執行用新的資料庫
        const QString database = stateDir + QStringLiteral("/quick-library.db");
        QFile::remove(database);
        if (auralis::core::migrateDatabase(database)) {
            m_services->databasePath = database;
        }
        m_library = std::make_unique<auralis::ui::LibraryController>(
            m_services->libraryManager, m_services->library, &m_services->dialogs,
            &m_services->notifier);
        auralis::ui::LibraryController::setInstance(m_library.get());
        // 設定頁的模組區塊槽位（tst_library）：一個正常、一個載不到的
        const QString qmlDir = QStringLiteral(QUICK_TEST_SOURCE_DIR);
        m_services->ui.addSettingsSection(
            {QStringLiteral("test/ok"), QStringLiteral("測試模組"),
             QUrl::fromLocalFile(qmlDir + QStringLiteral("/SettingsSectionFixture.qml")), 10});
        m_services->ui.addSettingsSection(
            {QStringLiteral("test/broken"), QStringLiteral("壞掉的模組"),
             QUrl::fromLocalFile(qmlDir + QStringLiteral("/NoSuchSection.qml")), 20});
        // 設定頁（tst_output：輸出面板的「輸出進階設定」開設定頁的輸出分區）
        m_services->ui.addPage(
            {QStringLiteral("settings"), QStringLiteral("設定"),
             QUrl(QStringLiteral("qrc:/qt/qml/Auralis/Ui/shell/SettingsPage.qml")), 900});
        // 曲目右鍵選單的模組區塊（tst_track_menu）：登記順序故意和 order 不同，含一個載不到的
        const auto trackMenuSection = [&](const char* id, const char* file, int order) {
            const QString path = qmlDir + QLatin1Char('/') + QString::fromLatin1(file);
            m_services->ui.addTrackMenuSection(
                {QString::fromLatin1(id), QString(), QUrl::fromLocalFile(path), order});
        };
        trackMenuSection("test/group", "TrackMenuGroupFixture.qml", 30);
        trackMenuSection("test/single", "TrackMenuItemFixture.qml", 20);
        trackMenuSection("test/broken", "NoSuchTrackMenuSection.qml", 15);
        trackMenuSection("test/sub", "TrackMenuSubFixture.qml", 10);
        // 工具列常駐項目（tst_toolbar，T-28）：登記順序故意和 order 不同，含一個載不到的；
        // 加一頁有自己工具列的頁面
        const auto toolbarItem = [&](const char* id, const char* file, int order) {
            const QString path = qmlDir + QLatin1Char('/') + QString::fromLatin1(file);
            m_services->ui.addToolbarItem(
                {QString::fromLatin1(id), QString(), QUrl::fromLocalFile(path), order});
        };
        toolbarItem("test/search", "ToolbarItemFixture.qml", 20);
        toolbarItem("test/broken", "NoSuchToolbarItem.qml", 15);
        toolbarItem("test/narrow", "ToolbarNarrowFixture.qml", 10);
        m_services->ui.addPage(
            {QStringLiteral("test/toolbar-page"), QStringLiteral("工具列測試頁"),
             QUrl::fromLocalFile(qmlDir + QStringLiteral("/ToolbarPageFixture.qml")), 950});
        // 全螢幕頁（tst_toolbar 的 test/toolbar-fullscreen）由測試自己用 nowPlayingProbe.addFixture
        // 登記：tst_now_playing_hooks 要驗沒有全螢幕頁（LYR 停用）時的樣子（T-76）
        engine->rootContext()->setContextProperty(QStringLiteral("testTmpDir"),
                                                  QString::fromUtf8(AURALIS_TEST_TMP));
        engine->rootContext()->setContextProperty(QStringLiteral("trackMenuProbe"),
                                                  &m_trackMenuProbe);
        engine->rootContext()->setContextProperty(QStringLiteral("libraryProbe"), &m_libraryProbe);
        engine->rootContext()->setContextProperty(QStringLiteral("fakePlayback"), &m_fake);
        engine->rootContext()->setContextProperty(QStringLiteral("nowPlayingProbe"),
                                                  &m_nowPlayingProbe);
        engine->rootContext()->setContextProperty(QStringLiteral("fakeOutputs"), &m_outputsProbe);
        engine->rootContext()->setContextProperty(QStringLiteral("jobsProbe"), &m_jobsProbe);
        m_frameProbe.theme = m_theme.get();
        engine->rootContext()->setContextProperty(QStringLiteral("frameProbe"), &m_frameProbe);
        m_plugins.install(engine, &m_services->dialogs); // 外掛分區（tst_plugins，T-104）
    }

private:
    std::unique_ptr<auralis::ui::ThemeController> m_theme;
    std::unique_ptr<auralis::ui::ShellState> m_shell;
    std::unique_ptr<auralis::core::CoreServices> m_services;
    auralis::test::FakePlaybackService m_fake;
    // 佇列與 Player 的查詢用到它，宣告在它們之前（後解構）
    NowPlayingProbe m_nowPlayingProbe;
    std::unique_ptr<auralis::ui::QueueModel> m_queue;
    std::unique_ptr<auralis::ui::PlayerController> m_player;
    auralis::test::FakeOutputs m_outputs;
    OutputsProbe m_outputsProbe{m_outputs};
    std::unique_ptr<auralis::ui::OutputController> m_output;
    LibraryProbe m_libraryProbe;
    std::unique_ptr<auralis::ui::LibraryController> m_library;
    TrackMenuProbe m_trackMenuProbe;
    JobsProbe m_jobsProbe;
    FrameProbe m_frameProbe;
    auralis::test::PluginsProbe m_plugins;
};

QUICK_TEST_MAIN_WITH_SETUP(ui_components, Setup)

#include "quick_test_main.moc"
