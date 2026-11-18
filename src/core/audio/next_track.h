#pragma once

#include <auralis/audio_source.h>
#include <auralis/track_ref.h>

#include <memory>
#include <optional>

namespace auralis::core {

/// 播放引擎要播的一首：音源，加上要播的區段（CUE 曲目）與上層的識別碼。
struct EngineTrack {
    std::unique_ptr<IAudioSource> source;
    /// 區段起點（音源的樣本框）；0 表示從頭。
    qint64 startFrame = 0;
    /// 區段終點（不含）；-1 表示播到音源結尾。
    qint64 endFrame = -1;
    /// 上層的識別碼（例：佇列項目編號），引擎不解讀，只在 PlaybackEngine::position() 原樣交回。
    quint64 token = 0;
    /// 從區段起點算的開始位置（音源的樣本框；暫停後繼續、跳轉時從中間開始）。
    /// position() 從這裡起算，所以位置一律是「從區段起點算」。
    qint64 offsetFrame = 0;
    /// 這一首的身分（佇列與 CUE 區段開檔時填入），交給處理器的 prepareTrack()；
    /// 不是從佇列來的音源是空的。
    TrackRef track;
};

/// 下一首提供者：播放引擎在目前曲目快播完時向它要下一首，做無縫接續（AC-05-1）。
/// 上層（PlaybackService 的佇列、CUE 區段）實作它，用 PlaybackEngine::setNextTrackProvider
/// 交給引擎。
class NextTrackProvider {
public:
    virtual ~NextTrackProvider() = default;
    /// 在**解碼執行緒**呼叫（第一首很短時，也可能在 play() 先填緩衝時於呼叫 play() 的執行緒），
    /// 每首曲目最多一次：剩約 5 秒時（長度不知道時是解到結尾時）。
    /// 回傳開好的下一首；nullopt 表示沒有下一首，目前這首播完就結束。
    /// 可以 I/O 與配置記憶體（例：用 SourceRegistry 開檔）；開不了的曲目由實作自己跳過。
    /// 上層的資料（佇列）由實作自己做跨執行緒保護。
    virtual std::optional<EngineTrack> nextTrack() = 0;
    /// 同 nextTrack()，多傳目前在解碼的那首的 token：撤回預開之後再問時，
    /// 上層不用自己追蹤解碼到哪一首。引擎呼叫這個；預設轉給 nextTrack()。
    virtual std::optional<EngineTrack> nextTrackAfter(quint64 token) {
        (void)token;
        return nextTrack();
    }
};

} // namespace auralis::core
