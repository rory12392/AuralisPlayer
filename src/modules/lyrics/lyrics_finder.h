#pragma once

#include "lyrics_document.h"

#include <auralis/track_ref.h>

#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace auralis {
class LibraryQuery;
}

namespace auralis::mod::lyr {

/// 找歌詞要用到的曲目資料（在工作執行緒由 resolveTrack 取得）。
struct LyricsTrack {
    TrackRef ref;
    /// 音檔本身；CUE 曲目是整軌檔（取不到時為空：沒有 .lrc 候選，內嵌歌詞核心也不給）。
    QString audioPath;
    /// CUE 曲目的軌號；整個檔案時是 0。
    quint32 cueTrack = 0;
    /// CUE 曲目在整軌檔裡的區段（毫秒）；整個檔案時是 0 與 -1。
    qint64 startMs = 0;
    qint64 endMs = -1;
    /// 查 LRCLIB 用的標籤（T-67）；不在音樂庫的檔案是空的（不查線上）。
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
};

/// 同名 .lrc 的候選路徑，依序找第一個存在的（AC-15-1）：
/// 一般曲目：同資料夾、同主檔名的 .lrc。
/// CUE 曲目：「整軌主檔名 + 空白 + 兩位數軌號」.lrc（例：Album.flac 第 3 軌 → Album 03.lrc），
/// 再退回整軌同名 .lrc（時間換算成曲目內的位置）。執行緒：任何執行緒（只組字串）。
QStringList lrcCandidates(const LyricsTrack& track);

/// 來源鏈的一段。find 在工作執行緒呼叫；沒找到回傳 std::nullopt，交給下一段。
/// 可預期的失敗（讀不到、檔案太大）也回傳 std::nullopt，不丟例外；可能只是暫時讀不到的
/// （內嵌歌詞：檔案被別的程式鎖住）回傳 status 是 Error 的結果，來源鏈照樣往下一段找。
class LyricsStep {
public:
    virtual ~LyricsStep() = default;
    virtual LyricsSource kind() const = 0;
    virtual std::optional<LyricsDocument> find(const LyricsTrack& track) = 0;
    /// 可以中途取消的版本（LRCLIB 的連線要能 abort，T-67）；預設不看 cancelled。
    virtual std::optional<LyricsDocument> find(const LyricsTrack& track,
                                               const std::function<bool()>& cancelled) {
        Q_UNUSED(cancelled);
        return find(track);
    }
    /// 來源鏈呼叫的版本：earlierFailed 是前面有段落這次讀取失敗（和不寫快取同一個條件）。
    /// 預設不看它；LRCLIB 用它決定要不要另存 .lrc（AC-16-4）。
    virtual std::optional<LyricsDocument>
    find(const LyricsTrack& track, const std::function<bool()>& cancelled, bool earlierFailed) {
        Q_UNUSED(earlierFailed);
        return find(track, cancelled);
    }
    /// 這一段目前有沒有開（LRCLIB 依設定 LYR/online）；關掉的段落直接跳過。任何執行緒呼叫。
    virtual bool active() const { return true; }
};

/// 第一段：同名 .lrc（經 TextDecoder 解碼，AC-15-4）。
class LrcFileStep final : public LyricsStep {
public:
    LyricsSource kind() const override { return LyricsSource::Lrc; }
    std::optional<LyricsDocument> find(const LyricsTrack& track) override;
};

/// 第二、三段：檔案內嵌的歌詞。有時間標籤時是內嵌同步歌詞，沒有時是內嵌純文字；
/// 核心只回傳一份內嵌歌詞，所以兩段合成一段，順序不變（同步的那份優先）。
class EmbeddedStep final : public LyricsStep {
public:
    explicit EmbeddedStep(const LibraryQuery& library) : m_library(library) {}
    LyricsSource kind() const override { return LyricsSource::Embedded; }
    std::optional<LyricsDocument> find(const LyricsTrack& track) override;

private:
    const LibraryQuery& m_library;
};

/// 可插入的來源清單（AC-15-1）：依加入順序找，第一個找到的就是結果。
/// T-65 加 .lrc 與內嵌兩段；LRCLIB（T-67）加在最後。
/// 執行緒：append 在第一次 find 之前、同一個執行緒呼叫；find 可以在任何執行緒同時呼叫。
class LyricsFinder {
public:
    void append(std::unique_ptr<LyricsStep> step);
    /// 全部來源都沒有時回傳 NotFound（source 是最後一段開著的段落的種類，給快取用）；
    /// 途中有一段回傳 Error（暫時讀不到）時改回傳 Error，不寫成可重用的 notfound；
    /// 之後的段落找到時照常回傳，但 cacheable 是 false（不寫快取）。
    /// cancelled 回傳 true 時停在兩段之間，回傳 std::nullopt（結果不完整，不寫快取）。
    std::optional<LyricsDocument> find(const LyricsTrack& track,
                                       const std::function<bool()>& cancelled) const;
    /// 目前最後一段開著的段落的種類：快取的 notfound 是別的種類時，表示來源鏈後來變長了
    /// （例：線上抓取從關改成開），那筆 notfound 不能再用。
    LyricsSource lastActiveKind() const;

private:
    std::vector<std::unique_ptr<LyricsStep>> m_steps;
};

/// 由 TrackRef 取得找歌詞用的曲目資料；CUE 曲目要查音樂庫（工作執行緒呼叫）。
LyricsTrack resolveTrack(const LibraryQuery& library, const TrackRef& ref);

} // namespace auralis::mod::lyr
