#pragma once

// 藝人頁與資料夾頁（T-23）測試共用：記憶體裡的音樂庫（專輯、曲目、資料夾）、設定、模型通知的計數。
#include "artists_model.h"
#include "folders_model.h"

#include <auralis/library_query.h>
#include <auralis/module_settings.h>

#include <QAbstractItemModel>
#include <QHash>

#include <atomic>
#include <mutex>

namespace auralis::test {

inline TrackInfo memTrack(qint64 albumId, const QString& path, const QString& artist,
                          qint64 folderId = 0) {
    TrackInfo t;
    t.albumId = albumId;
    t.folderId = folderId;
    t.ref = {path, 0};
    t.title = path;
    t.artist = artist;
    t.codec = QStringLiteral("FLAC");
    t.sampleRate = 44100;
    t.bits = 16;
    t.durationMs = 60'000;
    return t;
}

inline AlbumInfo memAlbum(qint64 id, const QString& title, const QString& albumArtist) {
    AlbumInfo a;
    a.id = id;
    a.title = title;
    a.albumArtist = albumArtist;
    return a;
}

/// 路徑的最後一段當名稱。
inline FolderInfo memFolder(qint64 id, qint64 parentId, const QString& path) {
    return {id, parentId, path, path.section(QLatin1Char('\\'), -1)};
}

/// 記憶體裡的音樂庫（專輯、曲目、資料夾）；內容可以換（模擬掃描中寫入）。
class MemoryLibrary final : public LibraryQuery {
public:
    void set(QList<AlbumInfo> albums, QList<TrackInfo> tracks, QList<FolderInfo> folders = {}) {
        std::lock_guard lock(m_mutex);
        m_albums = std::move(albums);
        m_tracks = std::move(tracks);
        m_folders = std::move(folders);
    }
    mutable std::atomic<int> trackCalls{0};
    std::atomic<bool> failFolders{false};
    Result<std::optional<TrackInfo>> findByIdentity(const QString&, quint32) const override {
        return std::optional<TrackInfo>();
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override {
        ++trackCalls;
        std::lock_guard lock(m_mutex);
        QList<TrackInfo> result;
        for (const TrackInfo& t : m_tracks) {
            if (filter.folderId == 0 || t.folderId == filter.folderId) {
                result.append(t);
            }
        }
        return result;
    }
    Result<QList<AlbumInfo>> albums() const override {
        std::lock_guard lock(m_mutex);
        return m_albums;
    }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64 parentId) const override {
        if (failFolders) {
            return Error{QStringLiteral("資料庫被鎖住"), 0, QStringLiteral("test")};
        }
        std::lock_guard lock(m_mutex);
        QList<FolderInfo> result;
        for (const FolderInfo& f : m_folders) {
            if (f.parentId == parentId) {
                result.append(f);
            }
        }
        return result;
    }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    void emitScanFinished() { emit scanFinished(); }

private:
    mutable std::mutex m_mutex;
    QList<AlbumInfo> m_albums;
    QList<TrackInfo> m_tracks;
    QList<FolderInfo> m_folders;
};

class MemorySettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return m_values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { m_values[key] = value; }
    bool contains(const QString& key) const override { return m_values.contains(key); }
    void remove(const QString& key) override { m_values.remove(key); }

private:
    QHash<QString, QVariant> m_values;
};

/// 模型發了幾次重設、插入、移除通知。
struct ModelLog {
    int resets = 0;
    int inserts = 0;
    int removes = 0;
    explicit ModelLog(QAbstractItemModel& model) {
        QObject::connect(&model, &QAbstractItemModel::modelReset, [this] { ++resets; });
        QObject::connect(&model, &QAbstractItemModel::rowsInserted, [this] { ++inserts; });
        QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [this] { ++removes; });
    }
};

} // namespace auralis::test
