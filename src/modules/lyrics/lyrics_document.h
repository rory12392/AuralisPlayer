#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace auralis::mod::lyr {

/// 歌詞的一行。純文字歌詞的 timeMs 一律是 -1。
struct LyricLine {
    qint64 timeMs = -1; ///< 已套用 [offset:]，不小於 0
    QString text;

    friend bool operator==(const LyricLine&, const LyricLine&) = default;
};

/// 歌詞從哪裡來；值和 lyrics_cache.source 相同。
enum class LyricsSource { Lrc, Embedded, Lrclib };

/// 找歌詞的結果（AC-15-1）。值型別，可以跨執行緒傳。
struct LyricsDocument {
    /// Unavailable：線上來源這次沒有回應、逾時或回應壞掉（AC-16-2）；不是永久的沒找到，不當快取。
    enum class Status { Found, NotFound, Error, Unavailable };

    Status status = Status::NotFound;
    LyricsSource source = LyricsSource::Embedded;
    /// 有時間標籤（同步歌詞）；false 時 lines 的 timeMs 都是 -1。
    bool synced = false;
    /// 純音樂（LRCLIB 回 instrumental，T-67）：status 是 Found、lines 是空的。
    bool instrumental = false;
    /// 來源鏈前面有段落暫時讀不到（例：內嵌被鎖住）時是 false：照常顯示，但不寫快取，
    /// 否則前段恢復後也換不回來（AC-15-1 的來源順序）。
    bool cacheable = true;
    /// 同步歌詞依時間排序（同時間保留原順序）；純文字照原順序。
    QList<LyricLine> lines;
};

/// 解析 LRC 或純文字歌詞（AC-15-4、模組規格 §1 的 LRC 解析）：
/// [mm:ss]、[mm:ss.x]、[mm:ss.xx]、[mm:ss.xxx]，一行可以有多個時間標籤；
/// [offset:+/-ms] 直接套進 timeMs（正值＝歌詞提前），[ti:] 等標頭整行忽略。
/// 有任何一行帶時間標籤時是同步歌詞，沒有時間標籤的文字行丟掉；都沒有時是純文字。
/// 執行緒：任何執行緒（純函式）。
LyricsDocument parseLyrics(const QString& text);

/// 位置（毫秒）落在哪一行：timeMs ≤ positionMs 的最後一行（二分搜尋）。
/// 第一行之前、沒有行或不是同步歌詞時回傳 -1。執行緒：任何執行緒。
int lineIndexAt(const LyricsDocument& document, qint64 positionMs);

/// 把歌詞寫回文字（同步歌詞寫成 [mm:ss.xxx]，offset 已經套進時間），給快取存；
/// parseLyrics(serializeLyrics(d)) 的 lines 與 synced 和 d 相同。執行緒：任何執行緒。
QString serializeLyrics(const LyricsDocument& document);

/// CUE 曲目借用整軌 .lrc 時，把時間換成曲目內的位置：減掉 startMs，
/// 只留下 [startMs, endMs) 之間的行（endMs < 0 表示到檔尾）。純文字歌詞原樣回傳。
LyricsDocument sliceLyrics(const LyricsDocument& document, qint64 startMs, qint64 endMs);

} // namespace auralis::mod::lyr
