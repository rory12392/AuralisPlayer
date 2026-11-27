#pragma once

#include "audio/next_track.h"
#include "audio/processor_chain_impl.h"
#include "audio/ring_buffer.h"
#include "audio/sample_convert.h"
#include "audio/volume_processor.h"

#include <auralis/audio_output.h>
#include <auralis/audio_source.h>
#include <auralis/dsd.h>

#include <QStringList>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace auralis::core {

/// 播放引擎：解碼執行緒（音源 → 處理器鏈或 DSD 包裝器 → 環形緩衝）＋輸出後端的即時音訊執行緒
/// （環形緩衝 → 裝置）。佇列、上下首、狀態訊號由 PlaybackService 在上層處理。
/// 位元完美：沒有處理器作用時整數樣本原封不動直通到後端。
/// 無縫（US-05）：目前曲目剩約 5 秒時向 NextTrackProvider 要下一首；格式相同就在同一個裝置上
/// 緊接著寫進緩衝（中間沒有任何靜音），不同時排空緩衝、送短靜音、重開裝置再接著播。
/// 執行緒：公開方法在非即時執行緒呼叫，同一時間只有一條（PlaybackService 的 UI 執行緒，
/// 或它開始播放時的背景執行緒，兩者交接時有同步）；setVolume() 例外。
/// render() 由後端的音訊執行緒呼叫。
class PlaybackEngine final : private IRenderSource {
public:
    /// 一首曲目開始播放時的狀態。
    struct Status {
        /// 音源交出的格式。
        AudioFormat sourceFormat;
        /// 交給輸出後端的格式（PCM 與來源相同；DSD 是包裝器的輸出格式）。
        AudioFormat outputFormat;
        /// 後端回報的裝置實際格式。
        AudioFormat deviceFormat;
        QString backend;
        bool bitPerfect = true;
        /// 不是位元完美時，改變訊號的處理器各自的原因（輸出路徑的原因在最前面）。
        QStringList reasons;
        /// 輸出路徑本身改變訊號的原因（共享模式、DSD 轉 PCM、浮點轉整數），開裝置時決定。
        QStringList pathReasons;
        /// 這首能不能調音量：DSD 經 DoP 或原生 DSD 直通時，任何增益都會把位元流變成噪音，所以不能；
        /// 轉 PCM 時可以（音量接在包裝器之後）。
        bool volumeAvailable = true;
        /// 音量不是 100% 而且能調時的原因「音量 n%」（也在 reasons 的最後一項）；否則為空。
        QString volumeReason;
    };

    explicit PlaybackEngine(ProcessorChainImpl& processors);
    ~PlaybackEngine() override;
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    /// DSD 音源要經過的包裝器（DoP、原生 DSD、轉 PCM，由進階輸出模組提供）；
    /// nullptr 表示不能播 DSD。從下一首開始生效。
    void setDsdSink(std::shared_ptr<DsdSink> sink);

    /// 音量（0–100%，預設 100）。100% 時不經過任何處理，整數樣本直通（位元完美）；其他值時
    /// 64 位元浮點處理後 TPDF 抖動回原位元深度，接在處理器鏈之後。播放中改變時不重開裝置：
    /// 增益以短斜坡連續過渡，緩衝裡還沒送出的資料（留約 0.1 秒）收回來用新音量重做，
    /// 所以大約 0.1 秒多就聽得到。DoP、原生 DSD 的位元流不受影響
    /// （status().volumeAvailable 為 false）；DSD 轉 PCM 時接在包裝器之後，
    /// 播放中改變不重做緩衝，約緩衝長度（0.5 秒）後聽得到。
    /// 任何非即時執行緒都可以呼叫。
    void setVolume(int percent) noexcept;
    int volume() const noexcept { return m_volume.percent(); }

    /// 正在聽到的曲目與位置。
    struct Position {
        /// EngineTrack::token（用 play(source, backend) 開始的那首是 0）。
        quint64 token = 0;
        /// 這首（區段）已經送進裝置的樣本框數，單位是輸出格式（status().outputFormat）的樣本框。
        qint64 frames = 0;
    };

    /// 下一首提供者；nullptr 表示播完目前這首就結束。provider 要活到 stop() 之後或換掉為止。
    /// 可以在播放中設定，從下一次要下一首時生效。
    void setNextTrackProvider(NextTrackProvider* provider) noexcept;

    /// 停掉目前的曲目，開始播 source。先在呼叫端執行緒把緩衝填滿才啟動後端，所以第一個裝置緩衝
    /// 就有資料。backend 在 stop() 之前都要有效。失敗時回傳 Error，引擎回到停止狀態。
    Result<Status> play(std::unique_ptr<IAudioSource> source, IOutputBackend& backend);
    /// 同上，可以指定區段與識別碼（CUE 曲目從區段起點播到終點）。
    /// cancelled 回傳 true 時（開裝置之後、預填的每個區塊之間、啟動後端之前檢查）放棄：
    /// 回到停止狀態、回傳 Error，不啟動後端（開始播放的要求已經被更新的操作取代）。
    Result<Status> play(EngineTrack track, IOutputBackend& backend,
                        const std::function<bool()>& cancelled = {});
    /// 停止後端與解碼執行緒並關閉裝置。沒有在播時什麼都不做。
    void stop();
    /// 跳到正在聽的曲目的 frame（音源樣本框，從區段起點算），不重開裝置：解碼執行緒請音訊執行緒
    /// 丟掉緩衝裡舊位置的資料、音源 seek、重新填緩衝。回傳 false 表示這時做不到（沒在播、已經解完、
    /// 或下一首已經寫進緩衝），上層改用 play() 帶 offsetFrame 重來。
    /// 回傳 true 之後、解碼執行緒處理之前就換到下一首時，這次跳轉放棄。
    bool seek(qint64 frame);
    /// 撤回已預開、還沒開始解碼的下一首（佇列改變時用）：解碼執行緒丟掉它，之後重新向提供者要。
    /// 下一首已經寫進緩衝時撤不回，上層在 position().token 換過去時自己處理。
    void invalidateNextTrack() noexcept;

    bool isPlaying() const noexcept { return m_backend != nullptr; }
    /// 曲目已解碼到結尾（或解碼失敗），沒有下一首，而且緩衝裡的資料都送進裝置了。
    bool isFinished() const noexcept;
    /// 解碼中途失敗的原因（isFinished() 之後才有意義）；沒有失敗時為空。
    /// 換格式時重開裝置失敗也在這裡（position().token 是開不了的那首，
    /// 種類看後端的 lastErrorKind()，處理方式和 play() 失敗相同）。
    std::optional<Error> decodeError() const;
    /// 最近一次開輸出是不是因為 DSD 包裝器和輸出路徑不相容而沒開（DoP、原生 DSD 遇到會改動訊號的
    /// 路徑，例：WASAPI 共享）：上層當成裝置不支援這首而跳過（AC-02-2）。play() 失敗後在同一條
    /// 執行緒讀。
    bool dsdPathRejected() const noexcept { return m_dsdPathRejected; }
    /// 正在聽到的曲目與位置（同格式接續時，換到下一首的時間點以送進裝置的資料為準）。
    Position position() const;
    /// 播放中輸出裝置失敗（被拔除、被佔用，AC-02-4）的原因：後端已停止取資料，
    /// 上層要呼叫 stop() 並顯示原因，裝置恢復後再 play()。沒有失敗或沒有在播時為空。
    std::optional<OutputError> outputError() const {
        return m_backend ? m_backend->playbackError() : std::nullopt;
    }
    /// 目前的狀態；處理器設定中途改變時 bitPerfect 與 reasons 會跟著變。
    /// 同格式接續時跟著解碼端換成下一首（比聽到的早約緩衝長度）。
    Status status() const;
    /// 這次開啟裝置以來，從緩衝送進裝置的樣本框數（不含補的靜音；換格式重開裝置時歸零）。
    qint64 renderedFrames() const noexcept {
        return m_renderedFrames.load(std::memory_order_relaxed);
    }
    /// 欠載次數：解碼還沒結束、緩衝卻不夠裝置要的量（換格式時刻意排空的不算）。
    qint64 underruns() const noexcept { return m_underruns.load(std::memory_order_relaxed); }

private:
    /// 解碼執行緒一次處理的樣本框數。
    static constexpr qint64 kChunkFrames = 4096;
    /// 緩衝滿（或等裝置送完）時解碼執行緒的等待間隔。
    static constexpr auto kFullWait = std::chrono::milliseconds(2);
    /// 音量改變時緩衝裡留下不重做的長度（毫秒）：要夠解碼執行緒把音源移回去、解出第一個區塊。
    static constexpr int kVolumeKeepMs = 100;
    /// 兩次重做之間至少隔多久：拖曳滑桿時不會每個中間值都把音源移回去一次。
    static constexpr auto kVolumeRedoInterval = std::chrono::milliseconds(50);
    /// 每秒幾個樣本框；DSD 的一個樣本框是每聲道 32 個位元。
    static qint64 framesPerSecond(const AudioFormat& format) noexcept;

    /// 一首曲目從緩衝的哪個位置開始（以這次開啟裝置以來寫進緩衝的樣本框計）。
    struct TrackMark {
        quint64 token = 0;
        qint64 start = 0;
    };

    qint64 render(std::byte* dest, qint64 frames) noexcept override;

    /// 解碼一個區塊寫進緩衝；到結尾或失敗時回傳 false。呼叫前要確定緩衝有一個區塊的空間。
    bool produceChunk();
    /// play() 啟動後端前把緩衝填滿；cancelled 成立就停在區塊之間。回傳 false 表示已解到結尾或失敗
    /// （已標記解碼結束）。
    bool prefill(const std::function<bool()>& cancelled);
    /// 目前曲目解到結尾時呼叫：有下一首時換過去（格式不同時標記要重開裝置）並回傳 true。
    bool advanceSource();
    /// 剩約 5 秒時（或解到結尾時）向提供者要下一首，每首只問一次。
    void prefetchNext();
    /// 依曲目格式準備處理器或 DSD 包裝器、開裝置、配置緩衝；play() 與換格式時用。
    /// 目前的 DSD 包裝器能不能經過 backend：不改變位元流的包裝器（DoP、原生 DSD）遇到會改動訊號的
    /// 路徑（共享模式）時回傳 Error 並記下 dsdPathRejected()。
    Result<bool> checkDsdPath(const IOutputBackend& backend);
    /// track 交給處理器的 prepareTrack()。
    Result<Status> openOutput(const AudioFormat& sourceFormat, const TrackRef& track,
                              IOutputBackend& backend);
    /// 讓 track 成為目前解碼的曲目：移到區段起點、記下它在緩衝裡的開始位置。
    Result<bool> beginTrack(EngineTrack track);
    /// 換格式：排空緩衝、送短靜音、重開裝置、寫入短靜音後接著播下一首（解碼執行緒）。
    bool switchOutput();
    /// 寫入 milliseconds 毫秒的數位靜音（DSD 經包裝器轉成它的靜音封包）；緩衝沒空間時等。
    /// 等的時候要求停止就回傳 false。
    bool writeSilence(int milliseconds);
    /// 開裝置時（音訊執行緒沒在跑）準備 render 補的靜音：DSD 是包裝器轉換過的靜音碼，PCM 是 0。
    void prepareSilence();
    /// 即時路徑：把 bytes 個位元組補成靜音，接續上一個送出的框的封包相位（DoP 標記交替）。
    void padSilence(std::byte* dest, std::size_t bytes) noexcept;
    void writeToRing(const std::byte* data, qint64 frames);
    /// 處理 seek() 的要求（解碼執行緒）；到結尾或失敗時回傳 false。
    bool handleSeek();
    /// 處理 invalidateNextTrack()（解碼執行緒）。
    void dropNext();
    /// 音量改變後（解碼執行緒）：請音訊執行緒把緩衝裡還沒送出的資料從尾端收回（留 kVolumeKeepMs），
    /// 音源移回對應的位置，之後的區塊用新音量重做。只收回目前解碼的這首、增益已經穩定之後的部分。
    /// 音源移不回去時回傳 false。
    bool redoForVolume();
    /// DSD 曲目開裝置前：目前的包裝器轉 PCM 時準備音量（斜坡長度、浮點緩衝）並回傳 true；
    /// DoP、原生 DSD 回傳 false（音量不可用）。
    bool prepareDsdVolume(const AudioFormat& outputFormat, qint64 outputChunkFrames);
    /// DSD 的 frames 個樣本框經包裝器轉換後寫進緩衝；轉 PCM 時先套用音量。
    void produceDsdChunk(qint64 frames);
    /// 即時路徑（render 開頭）：照 redoForVolume() 的要求收回緩衝尾端的資料。
    void serveUnwrite() noexcept;
    /// 音源樣本框換算成輸出格式的樣本框（DSD 包裝後框數不同）。
    qint64 toOutputFrames(qint64 sourceFrames) const noexcept;
    bool hasRoomForChunk() const noexcept;
    void decodeLoop();
    void finishDecoding(std::optional<Error> error);

    ProcessorChainImpl& m_chain;
    std::shared_ptr<DsdSink> m_dsdSink;
    std::atomic<NextTrackProvider*> m_provider{nullptr};

    // 以下在 play() 設定、stop() 清掉；播放中只有解碼執行緒碰（m_ring 與原子變數除外）。
    // 解碼執行緒換曲目時改 m_processors、m_status、m_marks 要拿 m_stateMutex，
    // 其他執行緒讀它們（status()、position()）也要拿。
    IOutputBackend* m_backend = nullptr;
    std::unique_ptr<IAudioSource> m_source;
    std::vector<std::unique_ptr<IProcessor>> m_processors;
    std::shared_ptr<DsdSink> m_activeDsdSink;
    Status m_status;
    mutable std::mutex m_stateMutex;
    std::deque<TrackMark> m_marks;
    /// seek() 已要求、解碼執行緒還沒重新對齊標記：這段時間 position() 回報跳轉的目標，
    /// 不回報舊位置（上層輪詢讀到舊位置會讓進度條先跳回去再跳過來）。
    bool m_seekInFlight = false;
    /// 目前解碼的曲目的區段起點與終點（跳轉用）。
    qint64 m_trackStart = 0;
    qint64 m_trackEnd = -1;
    /// 區段還剩幾個樣本框可讀；-1 表示讀到音源結尾。
    qint64 m_segmentLeft = -1;
    /// 預估還剩幾個樣本框（決定何時預開下一首）；-1 表示不知道。
    qint64 m_expectedLeft = -1;
    bool m_askedNext = false;
    std::optional<EngineTrack> m_next;
    bool m_reopenPending = false;
    qint64 m_writtenFrames = 0;
    /// 目前解碼的曲目從 m_writtenFrames 的哪裡開始寫（音量重做最多收回到這裡）。
    qint64 m_trackWriteStart = 0;
    /// 寫到這裡為止增益還在過渡（音量重做不收回這之前的資料，接續處才連續）。
    qint64 m_gainSettledAt = 0;
    /// 音源下一次 read() 的位置（音源樣本框）。
    qint64 m_sourceFrame = 0;
    std::chrono::steady_clock::time_point m_lastVolumeRedo{};
    std::unique_ptr<RingBuffer> m_ring;
    std::vector<std::byte> m_readBuffer;
    std::vector<std::byte> m_dsdBuffer;
    std::vector<double> m_floatBuffer;
    TpdfDither m_dither;
    /// 音量：接在 m_processors 之後；整個引擎一個，換曲時增益連續。
    VolumeProcessor m_volume;
    std::optional<Error> m_error;
    /// openOutput() 因 DSD 包裝器和輸出路徑不相容而沒開裝置（dsdPathRejected()）。
    bool m_dsdPathRejected = false;
    std::thread m_decodeThread;

    // 即時路徑讀的值：play() 啟動後端前寫好，播放中不變
    int m_outputFrameBytes = 0;
    /// render 補的靜音（DSD 曲目才有：包裝器轉換過的一個區塊）；空的表示補 0。
    std::vector<std::byte> m_silence;
    qint64 m_silenceFrames = 0;
    // 只有音訊執行緒碰（開裝置時歸零）：從緩衝送出與丟掉的樣本框數、下一個補的框在 m_silence 的位置
    qint64 m_ringFramesOut = 0;
    qint64 m_silencePhase = 0;

    std::atomic<bool> m_stopDecoding{false};
    std::atomic<bool> m_decodeDone{false};
    std::atomic<qint64> m_renderedFrames{0};
    std::atomic<qint64> m_underruns{0};
    /// render 補靜音的樣本框數（換格式時確認裝置已在送靜音才關）。
    std::atomic<qint64> m_paddedFrames{0};
    /// 換格式或跳轉中：刻意排空緩衝，補靜音不算欠載。
    std::atomic<bool> m_switching{false};
    /// 跳轉：解碼執行緒設 true，音訊執行緒丟掉緩衝裡的資料後設回 false。
    std::atomic<bool> m_flushRequest{false};
    /// seek() 的要求：位置與當時正在聽的曲目。
    std::atomic<bool> m_seekPending{false};
    std::atomic<qint64> m_seekFrame{0};
    std::atomic<quint64> m_seekToken{0};
    /// invalidateNextTrack() 的要求。
    std::atomic<bool> m_dropNext{false};
    /// setVolume() 之後還沒重做緩衝裡的資料。
    std::atomic<bool> m_volumeDirty{false};
    /// 音量重做：解碼執行緒設 m_unwriteRequest 並停著等，音訊執行緒收回緩衝尾端的資料
    /// （留 m_unwriteKeep 個樣本框、最多收回 m_unwriteMax 個），收回的框數寫進 m_unwritten，
    /// 再把 m_unwriteRequest 設回 false。
    std::atomic<bool> m_unwriteRequest{false};
    std::atomic<qint64> m_unwriteKeep{0};
    std::atomic<qint64> m_unwriteMax{0};
    std::atomic<qint64> m_unwritten{0};
};

} // namespace auralis::core
