#pragma once

// 歌詞模組測試共用：記憶體裡的音樂庫（內嵌歌詞、CUE 曲目）、暫存資料夾、樣本與字碼頁轉換、
// 升級到最新 schema 的測試資料庫。
#include "library/migrator.h"
#include "library/module_database_impl.h"
#include "test_qt_app.h"

#include <auralis/library_query.h>

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QThread>

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace auralis::test {

inline void ensureApp() {
    ensureQtApp("auralis_lyr_tests");
}

/// 跑事件迴圈直到 condition 成立或逾時；回傳 condition 的最後結果。
inline bool waitFor(const std::function<bool()>& condition, int timeoutMs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return condition();
}

/// 每個測試一個新的暫存資料夾（.localdata\tmp 底下，前綴 lyr-）。
inline std::unique_ptr<QTemporaryDir> lyrTempDir() {
    auto dir = std::make_unique<QTemporaryDir>(QString::fromUtf8(AURALIS_TEST_TMP) +
                                               QStringLiteral("/lyr-XXXXXX"));
    REQUIRE(dir->isValid());
    return dir;
}

inline void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

/// tests/modules/lyrics/samples/<name> 的原始位元組（UTF-8）。
inline QByteArray lyrSample(const QString& name) {
    QFile file(QStringLiteral(AURALIS_LYR_SAMPLES "/") + name);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

/// UTF-8 轉成 Windows 字碼頁（932＝Shift-JIS、950＝Big5）；有字元轉不過去時丟例外。
inline QByteArray toCodePage(const QByteArray& utf8, unsigned codePage) {
    const std::wstring wide = QString::fromUtf8(utf8).toStdWString();
    BOOL lossy = FALSE;
    const int size = WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, &lossy);
    QByteArray out(size, '\0');
    WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size,
                        nullptr, &lossy);
    if (lossy) {
        throw std::runtime_error("樣本有字元無法轉成目標字碼頁");
    }
    return out;
}

/// 升級到最新 schema 的資料庫，用 LYR 的 ModuleDatabase 開。
struct LyrDatabase {
    std::unique_ptr<QTemporaryDir> dir = lyrTempDir();
    QString path = dir->filePath(QStringLiteral("library.db"));
    std::unique_ptr<core::ModuleDatabaseImpl> db;
    LyrDatabase() {
        REQUIRE(core::migrateDatabase(path).hasValue());
        db = std::make_unique<core::ModuleDatabaseImpl>(QStringLiteral("LYR"), path);
    }
};

/// 記憶體裡的音樂庫：內嵌歌詞依路徑給；CUE 曲目的區段依 (path, subsong) 給。
class LyrLibrary final : public LibraryQuery {
public:
    void setEmbedded(const QString& path, const QString& lyrics) {
        std::lock_guard lock(m_mutex);
        m_embedded.insert(pathKey(path), lyrics);
    }
    void addCueTrack(const QString& cuePath, quint32 subsong, qint64 startFrame, qint64 endFrame,
                     const QString& audioPath = QString()) {
        {
            std::lock_guard lock(m_mutex);
            m_audio.insert(pathKey(cuePath) + QString::number(subsong), audioPath);
        }
        TrackInfo t;
        t.ref = {cuePath, subsong};
        t.startCdFrame = startFrame;
        t.endCdFrame = endFrame;
        std::lock_guard lock(m_mutex);
        m_cue.insert(pathKey(cuePath) + QString::number(subsong), t);
    }
    /// 一般曲目的標籤（查 LRCLIB 用，T-67）。
    void addTrack(const QString& path, const QString& title, const QString& artist,
                  const QString& album, qint64 durationMs) {
        TrackInfo t;
        t.ref = {path, 0};
        t.title = title;
        t.artist = artist;
        t.album = album;
        t.durationMs = durationMs;
        std::lock_guard lock(m_mutex);
        m_cue.insert(pathKey(path) + QString::number(0), t);
    }
    /// 有封面的一般曲目（全螢幕底色取主色用，T-69）。
    void addCoverTrack(const QString& path, const QString& coverKey) {
        TrackInfo t;
        t.ref = {path, 0};
        t.coverKey = coverKey;
        std::lock_guard lock(m_mutex);
        m_cue.insert(pathKey(path) + QString::number(0), t);
    }
    /// 讀這個檔案的內嵌歌詞時多花 ms 毫秒（模擬慢的磁碟，測試換歌）。
    void setDelay(const QString& path, int ms) {
        std::lock_guard lock(m_mutex);
        m_delay.insert(pathKey(path), ms);
    }
    /// 接下來 times 次讀這個檔案的內嵌歌詞回傳錯誤（模擬檔案被別的程式鎖住）。
    void failEmbedded(const QString& path, int times) {
        std::lock_guard lock(m_mutex);
        m_failures.insert(pathKey(path), times);
    }
    mutable std::atomic<int> embeddedCalls{0};
    /// 「核心已經解構」的替身（P-LYR-10）：設成 true 之後每次查詢記一次。不真的解構，
    /// 否則用到時是未定義行為、不一定當掉（這裡沒有 ASan）。
    std::atomic<bool> gone{false};
    mutable std::atomic<int> usedAfterGone{0};

    Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                    quint32 subsong) const override {
        touch();
        std::lock_guard lock(m_mutex);
        const auto it = m_cue.constFind(pathKey(path) + QString::number(subsong));
        if (it == m_cue.cend()) {
            return std::optional<TrackInfo>();
        }
        return std::optional<TrackInfo>(*it);
    }
    /// 一般曲目回傳 ref.path；CUE 曲目回傳 addCueTrack 給的整軌檔，沒給時回傳錯誤（同核心）。
    Result<QString> audioFilePath(const TrackRef& ref) const override {
        touch();
        if (ref.subsong == 0) {
            return ref.path;
        }
        std::lock_guard lock(m_mutex);
        const QString audio = m_audio.value(pathKey(ref.path) + QString::number(ref.subsong));
        if (audio.isEmpty()) {
            return Error{QStringLiteral("沒有這首"), 0, QStringLiteral("audioFilePath")};
        }
        return audio;
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
        return QList<TrackInfo>();
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef& track) const override {
        ++embeddedCalls;
        touch();
        if (track.subsong != 0) {
            return QString(); // 核心的規則：CUE 曲目一律沒有內嵌歌詞
        }
        int delay = 0;
        QString lyrics;
        {
            std::lock_guard lock(m_mutex);
            if (int& left = m_failures[pathKey(track.path)]; left > 0) {
                --left;
                return Error{QStringLiteral("檔案被鎖住"), 0, QStringLiteral("readEmbeddedLyrics")};
            }
            delay = m_delay.value(pathKey(track.path));
            lyrics = m_embedded.value(pathKey(track.path));
        }
        QThread::msleep(static_cast<unsigned long>(delay));
        return lyrics;
    }

private:
    void touch() const {
        if (gone.load()) {
            ++usedAfterGone;
        }
    }

    mutable std::mutex m_mutex;
    QHash<QString, int> m_delay;
    mutable QHash<QString, int> m_failures; // 讀一次少一次，所以在 const 的讀取裡會改
    QHash<QString, QString> m_audio;
    QHash<QString, QString> m_embedded;
    QHash<QString, TrackInfo> m_cue;
};

} // namespace auralis::test
