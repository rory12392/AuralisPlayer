#pragma once

#include <auralis/library_query.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace auralis::mod::lib {

/// 使用者在搜尋框與篩選列設定的條件（AC-09-2、AC-09-3）。空字串或 0 表示不限。
struct SearchCriteria {
    QString text;
    QString artist;
    QString album;
    int year = 0;
    QString format; ///< formatKey() 的值：FLAC、WAV…；DSD 以倍率分組（DSD64、DSD128…）
    int sampleRate = 0;

    bool operator==(const SearchCriteria&) const = default;
};

/// 格式分組的鍵：codec 加 dsd_rate。PCM 就是 codec（FLAC、WAV…），DSD 是 DSD64、DSD128…，
/// 倍率是 dsd_rate ÷ 44100；沒有倍率的 DSD 是「DSD」。執行緒：任何執行緒。
QString formatKey(const QString& codec, int dsdRate);
inline QString formatKey(const TrackInfo& track) {
    return formatKey(track.codec, track.dsdRate);
}

/// 條件 → LibraryQuery 的篩選。DSD 倍率的格式同時限定 codec 與取樣率（DSD 的取樣率就是 DSD 率），
/// 和另外選的取樣率互相矛盾時回傳 std::nullopt（結果一定是 0 首，不用查）。
std::optional<TrackFilter> toTrackFilter(const SearchCriteria& criteria);

/// 拖放與右鍵選單超過 5,000 首時 TrackDragPayload.query 帶的查詢條件（T-25，模組規格 §1）。
/// 代表的曲目＝同時符合全部有給的條件：criteria（同搜尋與篩選）、folderId（含子資料夾）、
/// albumIds（這幾張專輯的全部曲目，依給的順序、碟號、軌號）。
struct DragQuery {
    SearchCriteria criteria;
    qint64 folderId = 0;
    QList<qint64> albumIds;

    bool operator==(const DragQuery&) const = default;
};

/// 序列化成 JSON 字串：{"lib":1, "text", "artist", "album", "year", "format", "sampleRate",
/// "folderId", "albumIds"}，空字串、0、空清單的鍵省略。任何執行緒。
QString toDragQuery(const DragQuery& query);
/// 讀回；不是這個格式（不是 JSON 物件或沒有 "lib":1）時回傳 std::nullopt。
std::optional<DragQuery> parseDragQuery(const QString& json);

/// 取樣率的顯示文字：44100 →「44.1 kHz」、96000 →「96 kHz」、2822400 →「2.8224 MHz」。
QString sampleRateText(int rate);

/// 篩選列各個 Chip 的選項（整個音樂庫出現過的值）。
struct FilterOptions {
    QStringList artists;    ///< artist 與 album_artist 合併、不分大小寫排序
    QStringList albums;     ///< 不分大小寫排序
    QList<int> years;       ///< 由新到舊，不含 0（沒有年份）
    QStringList formats;    ///< 依名稱排序，DSD 依倍率排在最後
    QList<int> sampleRates; ///< 由低到高
};

/// 逐批累積選項（音樂庫分批讀，每批交給 add），最後 finish() 排序去重。
class FilterOptionsBuilder {
public:
    void add(const QList<TrackInfo>& tracks);
    FilterOptions finish() const;

private:
    QSet<QString> m_artists;
    QSet<QString> m_albums;
    QSet<int> m_years;
    QSet<QString> m_formats;
    QSet<int> m_sampleRates;
};

} // namespace auralis::mod::lib
