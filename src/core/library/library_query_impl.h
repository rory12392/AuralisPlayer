#pragma once

#include <auralis/library_query.h>

#include <QMutex>

#include <memory>

namespace auralis::core {

class SqliteConnection;

/// LibraryQuery 的實作：自己一條唯讀用途的連線（WAL 下讀取不會被掃描的寫入擋住）。
/// 資料庫路徑在第一次查詢時才讀（主程式在建立核心服務之後才設定路徑）；路徑是空字串時查詢回傳錯誤。
/// 執行緒：查詢可以從任何執行緒呼叫；notifyScanFinished 在 UI 執行緒呼叫。
class LibraryQueryImpl final : public LibraryQuery {
    Q_OBJECT

public:
    /// databasePath 是核心服務的欄位，要活得比這個物件久。
    /// 刻意晚綁定：CoreServices 先建構、主程式之後才設 databasePath，所以存參考；
    /// 呼叫端的字串要活得比物件久。
    explicit LibraryQueryImpl(const QString& databasePath);
    /// 暫存字串會在建構後立刻消失（存下的參考懸空），所以在編譯期擋掉。
    explicit LibraryQueryImpl(QString&& databasePath) = delete;
    ~LibraryQueryImpl() override;

    Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                    quint32 subsong) const override;
    Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const override;
    Result<QList<AlbumInfo>> albums() const override;
    Result<QStringList> artists() const override;
    Result<QList<FolderInfo>> folders(qint64 parentId) const override;
    Result<QString> readEmbeddedLyrics(const TrackRef& track) const override;
    Result<QList<TrackInfo>> tracksForDragQuery(const QString& query) const override;
    Result<QString> audioFilePath(const TrackRef& ref) const override;

    /// 掃描或移除音樂資料夾完成後由核心呼叫，發出 scanFinished。
    void notifyScanFinished();

private:
    Result<SqliteConnection*> connection() const;

    const QString& m_databasePath;
    mutable QMutex m_mutex;
    mutable std::unique_ptr<SqliteConnection> m_db;
};

} // namespace auralis::core
