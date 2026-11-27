#pragma once

#include "library/file_enumerator.h"
#include "library/tag_reader.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

namespace auralis::core {

class SqliteConnection;
struct CueTrackRow;

/// 一次掃描的結果（寫進 scan_reports；details_json 放 failures 與是否取消）。
struct ScanReport {
    qint64 id = 0;
    qint64 startedAt = 0; ///< Unix 毫秒
    qint64 finishedAt = 0;
    int scanned = 0; ///< 檢查過的音樂檔（含沒有變動的）
    int added = 0;
    int updated = 0;
    int removed = 0;
    int failed = 0; ///< 讀不到的檔案與資料夾（不含跳過的連結）
    bool cancelled = false;
    QList<ScanFailure> failures;
    /// 跳過的符號連結與 junction：是刻意不跟隨，不是讀不到，所以不算進 failed、不放進 failures
    /// （details_json 的 skippedLinks）。
    QList<ScanFailure> skippedLinks;
};

/// 音樂庫已有的檔案（增量比對用）。
struct KnownFile {
    qint64 id = 0;
    qint64 size = 0;
    qint64 mtimeNs = 0;
    bool ok = false; ///< status 是 ok；不是的話下次掃描重讀
};

/// 資料夾名稱是不是「Disc／Disk／CD＋數字」（例：「Disc 1」「cd2」「CD.2」「Disk_03」「ＣＤ２」）：
/// NFKC 與大小寫折疊後比對，中間可以有空白、點、底線、連字號或沒有分隔；後面還有其他文字時不算。
/// 這種資料夾的曲目改用上一層資料夾分組成專輯（多片專輯合併成一張，T-91）。
bool isDiscFolderName(const QString& name);

/// 掃描寫入核心擁有的表（folders、files、tracks、albums、scan_reports）的集中處。
/// 資料庫錯誤一律丟 std::runtime_error（掃描是背景工作，由 JobRunner 的錯誤邊界接住）。
/// 執行緒：一個實例只在一個工作執行緒使用。
class LibraryWriter {
public:
    explicit LibraryWriter(SqliteConnection& db) : m_db(db) {}

    void begin();
    void commit();
    void rollback() noexcept;

    /// 某個音樂資料夾底下已有的資料夾（path_key → id）與檔案（path_key → KnownFile）。
    QHash<QString, qint64> knownFolders(qint64 rootId);
    QHash<QString, KnownFile> knownFiles(qint64 rootId);

    /// 新增資料夾；parentId 為 0 表示音樂資料夾本身。回傳 id。
    qint64 insertFolder(qint64 rootId, qint64 parentId, const EnumeratedFolder& folder);
    /// 寫入讀到標籤的檔案（files 與 subsong 0 的 tracks；有專輯名時寫 albums）。
    void writeTrack(qint64 folderId, const EnumeratedFile& file, const TagData& tags);
    /// 寫入 .cue（files 是 .cue 本身，tracks 是每一軌，subsong＝軌號，含 CUE 區段）；
    /// .cue 裡已經沒有的軌刪掉。rows 不能是空的。
    void writeCueTracks(qint64 folderId, const EnumeratedFile& cueFile,
                        const std::vector<CueTrackRow>& rows);
    /// 寫入讀不到的檔案：files.status = unreadable，移除它的曲目（整檔或 CUE 的每一軌）。
    void writeUnreadable(qint64 folderId, const EnumeratedFile& file, const QString& error);
    /// 音樂資料夾（library_roots）；path 已存在時沿用。回傳 id。
    qint64 ensureRoot(const QString& path);
    /// 全部音樂資料夾，依加入順序。
    QList<std::pair<qint64, QString>> roots();
    /// 移除音樂資料夾：底下的資料夾、檔案、曲目跟著刪除（外鍵 cascade）。
    void deleteRoot(const QString& path);
    void deleteFile(qint64 id);
    void deleteFolder(qint64 id);
    /// 資料夾封面：這個資料夾裡沒有內嵌圖的曲目改用資料夾的封面圖檔（沒有圖檔時清成 NULL）。
    /// 每次掃描對每個讀得到的資料夾呼叫，圖檔新增、改掉或刪掉時曲目跟著更新。
    void assignFolderCover(qint64 folderId, const EnumeratedFolder& folder);
    /// 依目前的曲目重算專輯的曲目數與封面（第一首有封面的曲目），刪掉沒有曲目的專輯與
    /// 沒有曲目在用的封面。
    void refreshAlbums();

    qint64 insertReport(qint64 startedAt);
    void finishReport(const ScanReport& report);
    /// 最近一次掃描報告；沒有時 id 為 0。
    ScanReport lastReport();

private:
    qint64 upsertFile(qint64 folderId, const EnumeratedFile& file, const QString& status,
                      const QString& error);
    /// tracks 的一列（已存在時原地更新，id 不變）；startCdFrame／endCdFrame 為 -1 時寫 NULL。
    /// coverKey 是內嵌圖的 key；空字串時寫 NULL（之後由 assignFolderCover 補資料夾封面）。
    void upsertTrack(qint64 fileId, qint64 folderId, quint32 subsong, qint64 startCdFrame,
                     qint64 endCdFrame, const TagData& tags, const QString& fallbackTitle,
                     const QString& coverKey = {});
    /// covers 加一列（key 已存在時不動，保留已建好的縮圖與主色）。
    void insertCover(const QString& key, const QString& source);
    QVariant albumId(qint64 folderId, const TagData& tags);
    /// 專輯歸屬的資料夾：Disc 資料夾（isDiscFolderName）是上一層，其他是自己。
    qint64 albumFolder(qint64 folderId);
    void exec(const QString& sql, const QVariantList& params = {});
    QList<QVariantList> rows(const QString& sql, const QVariantList& params = {});

    SqliteConnection& m_db;
    QHash<QString, qint64> m_albums; ///< 「專輯藝人\x1f專輯\x1f專輯資料夾 id」→ albums.id
    QHash<qint64, qint64> m_albumFolders; ///< 曲目所在資料夾 id → 專輯資料夾 id
};

} // namespace auralis::core
