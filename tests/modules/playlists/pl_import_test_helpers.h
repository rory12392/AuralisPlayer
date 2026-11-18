#pragma once

// 匯入測試（T-51）共用：假的對話框、通知、開頁，測試資料庫上的匯入服務（沒給 JobRunner，
// 在呼叫端的執行緒直接做）與記錄查過哪些路徑的檔案查詢。
#include "pl_test_helpers.h"
#include "playlist_import.h"
#include "playlist_store.h"
#include "playlists_controller.h"

#include "fake_playback_service.h"

#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/navigator.h>
#include <auralis/notifier.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <atomic>

namespace auralis::test {

class ImportDialogs final : public Dialogs {
public:
    void chooseFolder(const QString&, PathsCallback) override {}
    void chooseFiles(const QString& title, const QStringList& filters,
                     PathsCallback done) override {
        lastTitle = title;
        lastFilters = filters;
        pending = std::move(done);
    }
    void chooseSaveFile(const QString&, const QString&, const QStringList&,
                        PathsCallback) override {}
    void confirm(const ConfirmRequest&, std::function<void(bool)>) override {}
    QString lastTitle;
    QStringList lastFilters;
    PathsCallback pending;
};

class TextNotifier final : public Notifier {
public:
    void notify(const QString& text, Kind kind, const QString&, std::function<void()>) override {
        texts.append(text);
        kinds.append(kind);
    }
    QStringList texts;
    QList<Kind> kinds;
};

class FakeNavigator final : public Navigator {
public:
    bool open(const QString& page, const QVariantMap& params) override {
        opened.append(page + QLatin1Char(' ') + params.value(QStringLiteral("id")).toString());
        return true;
    }
    bool back() override { return false; }
    QString currentPage() const override { return {}; }
    QStringList opened;
};

inline void ensureImportApp() {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_pl_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
}

/// 播放清單樣本（唯讀，T-100）：檔名相同的兩組。
/// - Public：tests/fixtures/playlists-public，合成的假樣本（由真樣本改寫路徑與字串表產生，
///   結構相同），公開快照也有，一定跑。
/// - Private：tests/fixtures/playlists，使用者 foobar2000 的真樣本（含個人音樂庫路徑），
///   公開快照沒有；不在時測試 SKIP（skipWithoutPrivateSamples）。
enum class Samples { Public, Private };

inline QString fixture(Samples samples, const QString& name) {
    return (samples == Samples::Public
                ? QStringLiteral(AURALIS_SOURCE_DIR "/tests/fixtures/playlists-public/")
                : QStringLiteral(AURALIS_SOURCE_DIR "/tests/fixtures/playlists/")) +
           name;
}

/// 私有的真樣本不在（公開快照）時略過這個測試並印出原因。
inline void skipWithoutPrivateSamples() {
    if (!QFileInfo::exists(fixture(Samples::Private, QStringLiteral("test1.fpl")))) {
        SKIP("私有樣本 tests/fixtures/playlists 不在（公開快照不含），只跑公開的合成樣本");
    }
}

inline QByteArray sha256(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

/// UTF-8 字串轉成位元組（測試樣本用）。
inline QByteArray utf8(const char* text) {
    return QByteArray(text);
}

/// 單一執行緒的 JobRunner，hold() 排一個等待中的工作佔住執行緒：之後排的工作要等放開才跑，
/// 所以能在它開始前取消（工作仍會執行、看到 isCancelled）。M-04 審查：取消後的清理。
struct HeldJobs {
    std::atomic<bool> released{false};
    JobRunner jobs{1};

    HeldJobs() { hold(); }
    ~HeldJobs() { released = true; }
    void hold() {
        released = false;
        jobs.submit(QStringLiteral("佔住"), [this](JobContext&) {
            while (!released.load()) {
                QThread::msleep(1);
            }
        });
    }
    /// 放開，等全部工作做完並處理回到 UI 執行緒的呼叫（呼叫可能再排工作，所以做兩輪）。
    void drain() {
        released = true;
        for (int round = 0; round < 2; ++round) {
            REQUIRE(jobs.waitForDone(10000));
            QCoreApplication::processEvents();
        }
    }
    void cancelAndDrain() {
        jobs.cancelAll();
        drain();
    }
};

struct ImportFixture {
    PlDatabase db;
    mod::pl::PlaylistStore store{*db.db};
    ImportDialogs dialogs;
    TextNotifier notifier;
    FakeNavigator navigator;
    FakePlaybackService playback;
    mod::pl::PlaylistsController controller{store,   dialogs, notifier, {},
                                            nullptr, nullptr, &playback};
    mod::pl::PlaylistImporter importer{store, controller, dialogs, notifier, nullptr, &navigator};
    /// 被查詢過的路徑（找不到的判斷）；existing 裡的才算存在。
    QStringList queried;
    /// 每次查詢的起點（T-52：重新對應時是「現在的資料夾」，其他是空的）
    QStringList starts;
    QStringList existing;
    bool everythingExists = false;
    QDir temp{db.dir.path()};

    ImportFixture() {
        ensureImportApp();
        importer.setExistsCheck([this](const QString& path, const QString& start) {
            queried.append(path);
            starts.append(start);
            return everythingExists || existing.contains(path, Qt::CaseInsensitive);
        });
    }
    /// 在測試暫存寫一個檔，回傳完整路徑（Windows 分隔符）。
    QString write(const QString& name, const QByteArray& bytes) {
        const QString path = temp.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(bytes) == bytes.size());
        return QDir::toNativeSeparators(path);
    }
    QString native(const QString& relative) const {
        return QDir::toNativeSeparators(temp.filePath(relative));
    }
    /// 播放清單 id 的項目路徑（照順序）。
    QStringList paths(qint64 id) const {
        QStringList result;
        const auto items = store.items(id);
        for (const auto& item : items.value()) {
            result.append(item.track.path);
        }
        return result;
    }
    qint64 lastId() const {
        const auto list = store.list();
        return list.value().isEmpty() ? 0 : list.value().last().id;
    }
};

} // namespace auralis::test
