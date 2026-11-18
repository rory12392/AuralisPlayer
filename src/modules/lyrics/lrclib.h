#pragma once

#include "lyrics_document.h"
#include "lyrics_finder.h"

#include <auralis/result.h>

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace auralis::mod::lyr {

/// 線上抓取的兩個設定（LYR/online、LYR/saveLrc）。UI 執行緒寫、工作執行緒讀，所以用原子變數。
struct LrclibSwitches {
    std::atomic<bool> online{true};
    std::atomic<bool> saveLrc{false};
};

/// 連線參數。baseUrl 只有測試會改（本機假伺服器）；正式一律是 LRCLIB（安全底線：對外連線只有它）。
struct LrclibConfig {
    QString baseUrl = QStringLiteral("https://lrclib.net");
    /// AuralisPlayer/<版本> (https://github.com/rory12392/AuralisPlayer)
    QString userAgent;
    /// 一次請求從送出到收完的上限（AC-16-2）。
    int timeoutMs = 5000;
    /// 回應本體的上限；超過就中斷，當成暫時取不到（外部輸入）。
    qint64 maxResponseBytes = 2 * 1024 * 1024;
};

/// 429／503 沒有 Retry-After 或解析不了時的等待（T-99，模組規格 §1「被限流時」）。
inline constexpr qint64 kLrclibDefaultRetryAfterMs = 60000;

/// Retry-After 標頭的值換成要等的毫秒數：非負整數是秒數；HTTP 日期（IMF-fixdate）是到那個時間
/// 的差，已經過了是 0；空的或解析不了時 kLrclibDefaultRetryAfterMs。任何執行緒。
qint64 lrclibRetryAfterMs(const QByteArray& value, const QDateTime& nowUtc);

/// User-Agent 字串（模組規格 §1）。
QString lrclibUserAgent(const QString& version);

/// 去掉曲名結尾的版本後綴（「 - 2025 Remaster」「(Instrumental)」「[Live]」…），
/// 沒有後綴時原樣回傳（模組規格 §1「沒涵蓋的情況」第 1 條）。任何執行緒。
QString stripVersionSuffix(const QString& title);

/// GET /api/get?track_name&artist_name&album_name&duration（秒，四捨五入）。
QUrl lrclibGetUrl(const QString& baseUrl, const LyricsTrack& track, const QString& title);
/// GET /api/search?track_name&artist_name。
QUrl lrclibSearchUrl(const QString& baseUrl, const LyricsTrack& track, const QString& title);

/// /api/get 的回應本體：同步歌詞、純文字、純音樂時 Found；沒有歌詞時 NotFound；
/// 不是 JSON 物件時 Unavailable（回應壞掉，下次再查）。source 一律是 Lrclib。
LyricsDocument parseLrclibRecord(const QByteArray& body);
/// /api/search 的回應本體：取時長差 ≤ 3 秒（durationMs ≤ 0 時不比）且有 syncedLyrics 的第一筆；
/// 沒有符合的 NotFound；不是 JSON 陣列時 Unavailable。
LyricsDocument pickLrclibSearch(const QByteArray& body, qint64 durationMs);

/// 把抓到的歌詞另存為同名 .lrc（AC-16-4）：只在 .lrc 不存在時寫，先寫暫存檔再改名，
/// 不碰音樂檔。CUE 曲目寫「整軌主檔名 兩位數軌號.lrc」。寫了回傳 true，.lrc 已存在或沒有
/// 位置可寫時 false；失敗時回傳錯誤（給日誌與通知列）。工作執行緒呼叫。
Result<bool> saveLrcBeside(const LyricsTrack& track, const LyricsDocument& document);

/// 來源鏈最後一段：LRCLIB（AC-16-1）。設定關閉時 active() 是 false，整段跳過，
/// 也不建立 QNetworkAccessManager（AC-16-3）；開著時每次查詢在工作執行緒建一個、查完就刪。
/// 沒有回應、逾時、HTTP 錯誤、回應壞掉時回傳 Unavailable（AC-16-2，不寫成 notfound）；
/// cancelled 回傳 true（換歌）或設定改成關閉時立刻 abort。
/// 執行緒：find 在工作執行緒呼叫，可以同時呼叫（每次各自建連線）。
class LrclibStep final : public LyricsStep {
public:
    using SaveFailed = std::function<void(const QString& reason)>;

    LrclibStep(LrclibConfig config, std::shared_ptr<const LrclibSwitches> switches,
               SaveFailed saveFailed = {});

    LyricsSource kind() const override { return LyricsSource::Lrclib; }
    bool active() const override;
    std::optional<LyricsDocument> find(const LyricsTrack& track) override;
    std::optional<LyricsDocument> find(const LyricsTrack& track,
                                       const std::function<bool()>& cancelled) override;
    /// earlierFailed 時照常回傳結果，但不另存 .lrc：.lrc 在來源鏈第一段，
    /// 存了之後前段恢復也換不回來（和不寫快取同一個理由）。
    std::optional<LyricsDocument> find(const LyricsTrack& track,
                                       const std::function<bool()>& cancelled,
                                       bool earlierFailed) override;

    /// 被限流（429／503）後還要等多久才再送請求（毫秒，沒有在等時 0）。任何執行緒。
    qint64 cooldownRemainingMs() const;

private:
    struct Reply;
    Reply fetch(const QUrl& url, const std::function<bool()>& cancelled) const;
    LyricsDocument lookup(const LyricsTrack& track, const QString& title,
                          const std::function<bool()>& cancelled) const;

    LrclibConfig m_config;
    std::shared_ptr<const LrclibSwitches> m_switches;
    SaveFailed m_saveFailed;
    /// 限流冷卻到什麼時候（steady_clock 的毫秒；0＝沒有）。整個模組只有一個 LrclibStep，
    /// 所以冷卻是整段的，不是單首。工作執行緒可以同時查詢，所以用原子變數。
    mutable std::atomic<qint64> m_blockedUntilMs{0};
};

} // namespace auralis::mod::lyr
