#pragma once

// 匯出測試（T-54）共用：記錄查詢的假音樂庫、記錄儲存對話框與確認的假對話框、記憶體裡的設定。
#include "pl_import_test_helpers.h"

#include <auralis/library_query.h>
#include <auralis/module_settings.h>

#include <QHash>
#include <QVariantMap>

namespace auralis::test {

/// 音樂庫的替身：known 裡的身分找得到；findByIdentity 與 audioFilePath 記錄查過哪些路徑。
class ExportLibrary final : public LibraryQuery {
public:
    QHash<QString, TrackInfo> known; ///< key：path + "|" + subsong
    QHash<QString, QString> audioOf; ///< .cue 路徑 → 整軌檔
    mutable QStringList queried;
    mutable QStringList audioCalls;

    void add(const QString& path, quint32 subsong, const QString& artist, const QString& title,
             qint64 durationMs) {
        TrackInfo info;
        info.ref = {path, subsong};
        info.artist = artist;
        info.title = title;
        info.durationMs = durationMs;
        known.insert(path + QLatin1Char('|') + QString::number(subsong), info);
    }
    Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                    quint32 subsong) const override {
        queried.append(path);
        const QString key = path + QLatin1Char('|') + QString::number(subsong);
        if (!known.contains(key)) {
            return std::optional<TrackInfo>();
        }
        return std::optional<TrackInfo>(known.value(key));
    }
    Result<QList<TrackInfo>> tracks(const TrackFilter&) const override {
        return QList<TrackInfo>();
    }
    Result<QList<AlbumInfo>> albums() const override { return QList<AlbumInfo>(); }
    Result<QStringList> artists() const override { return QStringList(); }
    Result<QList<FolderInfo>> folders(qint64) const override { return QList<FolderInfo>(); }
    Result<QString> readEmbeddedLyrics(const TrackRef&) const override { return QString(); }
    Result<QString> audioFilePath(const TrackRef& ref) const override {
        audioCalls.append(ref.path);
        if (!audioOf.contains(ref.path)) {
            return Error{QStringLiteral("沒有"), 0, QStringLiteral("audioFilePath")};
        }
        return audioOf.value(ref.path);
    }
};

/// 儲存對話框與確認對話框的替身：記下請求，由測試決定回答。
class ExportDialogs final : public Dialogs {
public:
    void chooseFolder(const QString&, PathsCallback) override {}
    void chooseFiles(const QString&, const QStringList&, PathsCallback) override {}
    void chooseSaveFile(const QString& title, const QString& suggestedName,
                        const QStringList& filters, PathsCallback done) override {
        saveTitle = title;
        saveSuggested = suggestedName;
        saveFilters = filters;
        pendingSave = std::move(done);
    }
    void confirm(const ConfirmRequest& request, std::function<void(bool)> done) override {
        confirms.append(request);
        pendingConfirm = std::move(done);
    }
    QString saveTitle;
    QString saveSuggested;
    QStringList saveFilters;
    PathsCallback pendingSave;
    QList<ConfirmRequest> confirms;
    std::function<void(bool)> pendingConfirm;
};

/// settings.ini 的 PL/ 段的替身。
class MapSettings final : public ModuleSettings {
public:
    QVariant value(const QString& key, const QVariant& defaultValue) const override {
        return values.value(key, defaultValue);
    }
    void setValue(const QString& key, const QVariant& value) override { values.insert(key, value); }
    bool contains(const QString& key) const override { return values.contains(key); }
    void remove(const QString& key) override { values.remove(key); }
    QVariantMap values;
};

inline QByteArray readAll(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

/// 資料夾裡的檔名（檢查沒有留下暫存檔）。
inline QStringList filesIn(const QString& folder) {
    return QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
}

} // namespace auralis::test
