#pragma once

#include <auralis/library_query.h>

#include <QHash>
#include <QString>

#include <atomic>
#include <mutex>
#include <optional>

namespace auralis::mod::out {

/// ReplayGain 模式（AC-06-1），記在 settings.ini 的 OUT/replayGain（"off"、"track"、"album"），
/// 預設關閉（保持位元完美）。
enum class RgMode { Off, Track, Album };

QString rgModeKey(RgMode mode);
/// 不認得的值當成關閉。
RgMode rgModeFromKey(const QString& key);

/// 一個來源（rg_computed 或標籤）的增益與峰值：增益單位 dB，峰值是線性振幅（1.0＝滿刻度）。
/// 沒有資料的欄位是 std::nullopt。
struct RgValues {
    std::optional<double> trackGain, trackPeak, albumGain, albumPeak;
};

/// 增益從哪裡來。
enum class RgSource { None, Computed, Tags };

/// 某個模式下選到的增益。
struct RgChoice {
    /// 沒有任何增益可用時是 std::nullopt（不調整，維持位元完美）。
    std::optional<double> gainDb;
    /// 和增益同一個來源的峰值；來源沒有峰值時是 std::nullopt。
    std::optional<double> peak;
    RgSource source = RgSource::None;
    /// 專輯模式但這首沒有專輯增益，退回音軌增益（規格 §1 情況 4）。
    bool albumFallback = false;
};

/// 一首曲目在音軌與專輯兩種模式下各自選到的增益（播放中換模式時不用重查）。
struct RgTrackGain {
    RgChoice track;
    RgChoice album;

    const RgChoice& forMode(RgMode mode) const { return mode == RgMode::Album ? album : track; }
};

/// 選增益：每一個值都是 rg_computed 優先、其次標籤，峰值跟著增益的來源走。
/// 專輯模式沒有專輯增益時退回音軌增益並標 albumFallback。
RgTrackGain chooseGain(const RgValues& computed, const RgValues& tags);

/// 實際套用的增益（dB）。防削波時增益不超過 -20·log10(peak)，套用後的峰值不超過 0 dBFS（AC-06-2）；
/// 峰值不知道時防削波把增益限制在 0 dB 以下（只衰減不放大，整數來源就不會超過滿刻度）。
double appliedGainDb(const RgChoice& choice, bool preventClipping);

/// dB 轉線性倍率。
double dbToLinear(double db);

/// 設定的目前值：UI 執行緒寫，解碼執行緒的處理器每個區塊讀（原子變數，Architecture §8）。
struct RgSettings {
    std::atomic<RgMode> mode{RgMode::Off};
    std::atomic<bool> preventClipping{true};
};

/// 處理器實際替哪首曲目算出什麼增益：處理器在換曲時（解碼執行緒，非即時）寫，
/// 輸出面板的區塊在 UI 執行緒讀來說明目前這首的狀態。只記最近的幾首。
class RgAppliedLog {
public:
    void record(const TrackRef& track, const RgTrackGain& gain);
    std::optional<RgTrackGain> find(const TrackRef& track) const;

private:
    static QString key(const TrackRef& track);
    mutable std::mutex m_mutex;
    QHash<QString, RgTrackGain> m_entries;
};

/// 輸出面板與設定頁下方的狀態說明（UI 執行緒）。
/// dsdRoute 是正在播的 DSD 輸出方式（dsdRouteLabel；不是 DSD 時空字串）：DSD 一律不經過處理器鏈。
/// gain 是處理器替目前這首算的結果；還不知道時 std::nullopt（回空字串）。
QString rgStatusText(RgMode mode, bool preventClipping, const QString& dsdRoute,
                     const std::optional<RgTrackGain>& gain);

} // namespace auralis::mod::out
