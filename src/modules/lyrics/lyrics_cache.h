#pragma once

#include "lyrics_document.h"
#include "lyrics_finder.h"

#include <optional>

namespace auralis {
class ModuleDatabase;
}

namespace auralis::mod::lyr {

/// lyrics_cache 的讀寫（找到與沒找到都記，AC-15-1）。鍵是 (pathKey(ref.path), subsong)，
/// content 存 serializeLyrics 的結果（offset 已套用、CUE 已換成曲目內的時間）。
/// 失效：曲目的檔案（CUE 曲目看整軌檔）修改時間晚於 fetched_at；
/// 另外 .lrc 候選有變化時也失效（多了 .lrc 但快取不是 lrc、.lrc 不見了、.lrc 比快取新），
/// 否則使用者後來放進去的 .lrc 永遠讀不到；有兩個 .lrc 候選的 CUE 曲目來源是 lrc 時一律重讀。
/// status 是 error 的列不當成快取（下次重找）。
/// 執行緒：工作執行緒（會讀檔案資訊與資料庫）；可以同時呼叫。
class LyricsCache {
public:
    explicit LyricsCache(ModuleDatabase& database) : m_database(database) {}

    /// 有效的快取；沒有、已失效或讀不到時回傳 std::nullopt。
    std::optional<LyricsDocument> read(const LyricsTrack& track) const;
    /// 寫入（取代同一首的舊資料）；失敗只記日誌，回傳是否成功。
    bool write(const LyricsTrack& track, const LyricsDocument& document) const;

private:
    ModuleDatabase& m_database;
};

/// lyrics_cache.source 的文字。
QString sourceName(LyricsSource source);

} // namespace auralis::mod::lyr
