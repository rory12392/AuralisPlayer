#pragma once

#include <auralis/result.h>
#include <auralis/track_ref.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace auralis {

/// 路徑的比對鍵：去掉 \\?\（\\?\UNC\ 改回 \\）、分隔符統一為 \、Unicode NFC、大小寫折疊。
/// 模組的資料表以 (path_key, subsong) 關聯曲目時用它算 path_key。執行緒：任何執行緒。
inline QString pathKey(QString path) {
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (path.startsWith(QLatin1String("\\\\?\\UNC\\"), Qt::CaseInsensitive)) {
        path = QLatin1String("\\\\") + path.mid(8);
    } else if (path.startsWith(QLatin1String("\\\\?\\"))) {
        path = path.mid(4);
    }
    return path.normalized(QString::NormalizationForm_C).toCaseFolded();
}

/// 搜尋用的正規化：NFKC（全形英數轉半形）加大小寫折疊，例：「ＮｉｅＲ：Automata」→「nier:automata」。
/// 音樂庫的 search_text 與搜尋字串都經過它（AC-09-2）。執行緒：任何執行緒。
inline QString normalizeSearchText(const QString& text) {
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded();
}

/// tracks 一列的唯讀副本。數值欄沒有資料時為 0（rg* 沒有時為 std::nullopt）。
struct TrackInfo {
    qint64 id = 0; ///< 重新掃描後可能改變；跨掃描保存請用 ref
    TrackRef ref;
    qint64 albumId = 0;
    qint64 folderId = 0;
    QString title; ///< 沒有曲名標籤時是不含副檔名的檔名
    QString artist;
    QString albumArtist;
    QString album;
    QString composer;
    QString genre;
    int year = 0;
    int discNo = 0;
    int trackNo = 0;
    qint64 durationMs = 0;
    QString codec;      ///< FLAC、WAV、AIFF、ALAC、AAC、MP3、APE、WV、Opus、Vorbis、DSD
    int sampleRate = 0; ///< DSD 是 DSD 率（DSD64＝2822400）
    int bits = 0;       ///< DSD 是 1；有損格式是 0
    int channels = 0;
    int dsdRate = 0; ///< PCM 是 0
    std::optional<double> rgTrackGain, rgTrackPeak, rgAlbumGain, rgAlbumPeak;
    bool hasEmbeddedLyrics = false;
    QString coverKey;
    /// CUE 區段（1/75 秒）；整個檔案時為 -1。
    qint64 startCdFrame = -1;
    qint64 endCdFrame = -1;
};

struct AlbumInfo {
    qint64 id = 0;
    QString albumArtist;
    QString title;
    int year = 0;
    qint64 folderId = 0;
    QString coverKey;
    int trackCount = 0;
};

/// 資料夾樹的一個節點；parentId 為 0 表示是音樂資料夾（library root）本身。
struct FolderInfo {
    qint64 id = 0;
    qint64 parentId = 0;
    QString path;
    QString name;
};

/// 曲目篩選條件，各欄都設定時取交集（AC-09-3 的疊加）；空字串或 0 表示不限。
struct TrackFilter {
    /// 比對曲名、藝人、專輯藝人、專輯、作曲者；不分大小寫與全形半形（AC-09-2）。
    /// 以空白分成多個詞，全部都要出現。
    QString search;
    QString artist; ///< 比對 artist 或 album_artist（完全相同）
    QString album;
    int year = 0;
    QString codec;
    int sampleRate = 0;
    qint64 albumId = 0;
    qint64 folderId = 0; ///< 只限這個資料夾（不含子資料夾）
    int limit = -1;      ///< -1 表示不限
    int offset = 0;
};

/// 唯讀查詢音樂庫。查詢方法會讀資料庫或檔案，在工作執行緒呼叫（例：JobRunner 的工作），
/// 可以從多個執行緒同時呼叫；回傳值物件，不回傳資料庫游標。
/// scanFinished 訊號在 UI 執行緒發出。實例由核心擁有，模組經 ModuleHost::library() 取得。
class LibraryQuery : public QObject {
    Q_OBJECT

public:
    ~LibraryQuery() override = default;
    LibraryQuery(const LibraryQuery&) = delete;
    LibraryQuery& operator=(const LibraryQuery&) = delete;

    /// 依身分找曲目；path 比對的是 pathKey(path)，所以大小寫與分隔符不同也找得到。
    /// 沒有這首時回傳 std::nullopt。
    virtual Result<std::optional<TrackInfo>> findByIdentity(const QString& path,
                                                            quint32 subsong) const = 0;
    /// 依條件列出曲目，依專輯藝人、專輯、碟號、軌號、路徑排序。
    virtual Result<QList<TrackInfo>> tracks(const TrackFilter& filter) const = 0;
    /// 全部專輯，依專輯藝人、專輯名排序。
    virtual Result<QList<AlbumInfo>> albums() const = 0;
    /// 不重複的藝人（artist 與 album_artist 合併），依名稱排序。
    virtual Result<QStringList> artists() const = 0;
    /// parentId 的子資料夾；parentId 為 0 時回傳各音樂資料夾本身。依名稱排序。
    virtual Result<QList<FolderInfo>> folders(qint64 parentId) const = 0;
    /// 讀檔案內嵌的歌詞（ID3 USLT、Vorbis／APE 的 LYRICS 等），每次都讀檔、唯讀開啟。
    /// 沒有歌詞時回傳空字串；CUE 曲目（subsong 不是 0）一律回傳空字串（內嵌歌詞屬於整個檔案）。
    virtual Result<QString> readEmbeddedLyrics(const TrackRef& track) const = 0;
    /// 依 TrackDragPayload.query（只帶查詢條件的拖放與右鍵內容，超過 5,000 首時）取曲目（T-62）。
    /// query 是 JSON 字串 {"lib":1, text, artist, album, year, format, sampleRate, folderId,
    /// albumIds}（格式由音樂庫瀏覽模組定義），代表同時符合全部有給的條件的曲目：
    /// text 與五種篩選同音樂庫頁的搜尋與篩選（format 是 FLAC、WAV…、DSD64、DSD128…）；
    /// folderId 含子資料夾；albumIds 是這幾張專輯的全部曲目。
    /// 排序：有 albumIds 時依給的順序、碟號、軌號；沒有時同 tracks()。
    /// 不是這個格式（JSON 壞掉、lib 不是 1）時回傳錯誤；條件互相矛盾時回傳空清單。
    /// 預設實作回傳「不支援」錯誤（測試替身不用實作）；核心的實作會讀資料庫，在工作執行緒呼叫。
    virtual Result<QList<TrackInfo>> tracksForDragQuery(const QString& query) const {
        Q_UNUSED(query)
        return Error{QStringLiteral("不支援依查詢條件取曲目"), 0, QStringLiteral("LibraryQuery")};
    }
    /// 這首曲目實際解碼的音訊檔的完整路徑（T-77）：一般曲目回傳 ref.path 本身；CUE 曲目
    /// （ref.path 是 .cue）重新讀 .cue，照這一軌的 FILE 找整軌檔（規則和播放、
    /// SourceRegistry::openTrack 相同：寫的檔名不存在時找同資料夾、同主檔名的音樂檔），
    /// 回傳它的完整路徑（Windows 分隔符）。多個 FILE 的 .cue 不同軌回傳不同的檔。
    /// 音樂庫裡沒有這首、.cue 讀不到、.cue 裡沒有這一軌或指向的檔案不存在時回傳錯誤。
    /// 只讀檔，不改任何東西。預設實作回傳「不支援」錯誤（測試替身不用實作）；
    /// 核心的實作會讀資料庫與 .cue，在工作執行緒呼叫。
    virtual Result<QString> audioFilePath(const TrackRef& ref) const {
        Q_UNUSED(ref)
        return Error{QStringLiteral("不支援取曲目的音訊檔路徑"), 0, QStringLiteral("LibraryQuery")};
    }

signals:
    /// 音樂庫內容因掃描或移除音樂資料夾而更新完成（含被取消、只做了一部分的掃描）。
    void scanFinished();

protected:
    LibraryQuery() = default;
};

} // namespace auralis
