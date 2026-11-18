#pragma once

#include "replay_gain.h"

#include <auralis/audio_processor.h>

#include <functional>
#include <memory>

namespace auralis {
class LibraryQuery;
class ModuleDatabase;
} // namespace auralis

namespace auralis::mod::out {

/// 查一首曲目的增益（rg_computed 與標籤）。在解碼執行緒呼叫（非即時，可以 I/O）。
using RgLookup = std::function<RgTrackGain(const TrackRef& track)>;

/// 正式的查法：rg_computed（ModuleDatabase，以 pathKey 與 subsong 對應）優先，其次音樂庫的標籤
/// （LibraryQuery::findByIdentity）。查詢失敗當成沒有資料（不調整，維持位元完美）。
RgLookup libraryLookup(ModuleDatabase& database, LibraryQuery& library);

/// ReplayGain 處理器（AC-06-1、2、4）：每首曲目一個實例，在解碼執行緒執行。
/// 換曲時（prepareTrack，非即時）先查好增益，把音軌與專輯兩種模式、防削波開與關的倍率都算好；
/// process() 依目前設定（原子變數）選一個倍率相乘，不配置、不加鎖、不丟例外。
/// 作用中（模式不是關閉而且這首有增益）時輸出狀態寫「ReplayGain 已套用」、非位元完美（AC-06-4）。
/// DSD 不經過處理器鏈，所以 DoP、原生 DSD 與轉 PCM 時都不作用（核心的 DSD 路徑旁通）。
class ReplayGainProcessor final : public IProcessor {
public:
    ReplayGainProcessor(std::shared_ptr<const RgSettings> settings, RgLookup lookup,
                        std::shared_ptr<RgAppliedLog> log);

    /// 只有格式、不知道是哪一首：不作用（位元完美）。引擎實際呼叫的是 prepareTrack()。
    void prepare(const AudioFormat& format) override;
    /// 這次要處理的曲目（解碼執行緒）：查增益、算好倍率、記進給畫面看的紀錄。
    /// 引擎開始播放與無縫接續時都會呼叫（接續時拿到的是接續的那一首）；不是從佇列播的音源
    /// path 是空的，查不到增益，不作用。
    void prepareTrack(const AudioFormat& format, const TrackRef& track) override;

    bool isActive() const noexcept override;
    QString reason() const override;
    void process(double* samples, qint64 frames, int channels) noexcept override;

    /// 目前設定下的倍率（測試用）；不作用時是 1.0。
    double currentFactor() const noexcept;

private:
    /// [模式：0 音軌、1 專輯][防削波：0 關、1 開]
    double m_factor[2][2] = {{1.0, 1.0}, {1.0, 1.0}};
    bool m_has[2] = {false, false};
    std::shared_ptr<const RgSettings> m_settings;
    RgLookup m_lookup;
    std::shared_ptr<RgAppliedLog> m_log;
};

/// 註冊到 ProcessorChain 的工廠（名稱「ReplayGain」）。
class ReplayGainProcessorFactory final : public IProcessorFactory {
public:
    ReplayGainProcessorFactory(std::shared_ptr<const RgSettings> settings, RgLookup lookup,
                               std::shared_ptr<RgAppliedLog> log);
    QString name() const override;
    std::unique_ptr<IProcessor> create() override;

private:
    std::shared_ptr<const RgSettings> m_settings;
    RgLookup m_lookup;
    std::shared_ptr<RgAppliedLog> m_log;
};

} // namespace auralis::mod::out
