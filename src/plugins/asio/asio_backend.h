// ASIO PCM 輸出後端（US-03；只在 AURALIS_WITH_ASIO 時編譯）。
#pragma once

#include "asio_control_thread.h"
#include "asio_drivers.h"
#include "dop_sink.h"

#include <auralis/audio_output.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace auralis::mod::out {

/// 播放中多久沒有任何回呼就當成裝置被別的程式接手。ASIO 驅動被第二個程式開啟時
/// 不會回錯誤碼，先開的只是回呼停止（通常同時收到重設要求），所以要靠時間判斷。
inline constexpr std::chrono::milliseconds kAsioStallLimit{1000};

/// 原生 DSD 不能用時給使用者的提示（AC-04-2）；任何執行緒呼叫，接收端自己轉到 UI 執行緒。
using AsioNotice = std::function<void(const QString&)>;

/// 一個 ASIO 驅動的輸出（PCM 與 DSD）。驅動的控制呼叫都在自己的 STA 控制執行緒；
/// 驅動在它的執行緒呼叫回呼，回呼裡向 IRenderSource 要一個半緩衝的資料、轉成驅動的樣本格式。
/// 收到 DSD 格式（原生 DSD 包裝器交來的位元流）時先用 kAsioSetIoFormat 切原生 DSD（AC-04-4）；
/// 驅動不接受就提示「此驅動不支援原生 DSD，已改用 DoP」，並在同一個驅動上改送 DoP（AC-04-2）。
/// open() 回傳的裝置格式分得出兩者：原生是 DSD 格式，DoP 是 PCM（DSD ÷ 16、24 位元）。
/// ASIO 回呼沒有使用者指標，一個行程同時只能有一個開啟中的 ASIO 後端（第二個 open 回傳錯誤）。
/// 執行緒：IOutputBackend 的規則。
class AsioBackend final : public IOutputBackend {
public:
    /// window：給驅動 init 的視窗（主視窗 HWND；0 時用桌面視窗）。
    AsioBackend(std::shared_ptr<AsioDrivers> drivers, AsioDriverEntry driver, quintptr window,
                std::chrono::milliseconds stallLimit = kAsioStallLimit, AsioNotice notice = {});
    ~AsioBackend() override;
    AsioBackend(const AsioBackend&) = delete;
    AsioBackend& operator=(const AsioBackend&) = delete;

    QString name() const override;
    Result<AudioFormat> open(const AudioFormat& requested) override;
    Result<bool> start(IRenderSource& source) override;
    void stop() noexcept override;
    void close() noexcept override;
    OutputErrorKind lastErrorKind() const noexcept override { return m_lastErrorKind; }
    std::optional<OutputError> playbackError() const override;

    /// 以下由驅動的回呼轉進來（驅動的執行緒）。即時路徑：不配置、不加鎖、不 I/O、不呼叫 Qt。
    void onBufferSwitch(long index) noexcept;
    void onResetRequest() noexcept;

    /// DSD 曲目最近一次開啟走的路：沒有在播 DSD、原生 DSD、或 DoP 退路。
    enum class DsdPath { None, Native, Dop };
    DsdPath dsdPath() const noexcept { return m_dsdPath; }

private:
    Result<AudioFormat> openDriver(const AudioFormat& requested);
    /// 切原生 DSD 並設 DSD 位元率、確認聲道格式是 DSDInt8MSB1；任一步失敗就切回 PCM 回傳 false。
    bool tryNativeDsd(quint32 dsdRate, long channels);
    /// 半緩衝的靜音：PCM 是 0，原生 DSD 是靜音碼 0x69，DoP 是 0x69 包成的 DoP 封包（AC-04-6）。
    void prepareSilence();
    void releaseDriver() noexcept;
    /// 把兩個半緩衝都填成靜音（開始前、失敗後）。
    void fillSilence(long index) noexcept;
    void fail(OutputErrorKind kind) const noexcept;

    std::shared_ptr<AsioDrivers> m_drivers;
    AsioDriverEntry m_entry;
    quintptr m_window;
    std::chrono::milliseconds m_stallLimit;
    AsioNotice m_notice;
    AsioControlThread m_control;

    // 以下只在控制執行緒動
    struct IASIO* m_driver = nullptr;
    bool m_buffersCreated = false;
    bool m_running = false;
    /// 驅動被切到 DSD：釋放前切回 PCM，留給下一首（或下一個程式）的是 PCM 狀態。
    bool m_ioFormatDsd = false;

    // 以下在 open() 設定，播放中不變（回呼只讀）
    /// 交給 deinterleaveToAsio 的 PCM 格式（DoP 退路時是 DoP 的格式）；原生 DSD 時是 DSD 格式。
    AudioFormat m_requested;
    DsdPath m_dsdPath = DsdPath::None;
    long m_bufferFrames = 0;
    /// 每次回呼向來源要幾個樣本框（PCM 等於緩衝框數；原生 DSD 是 ÷ 32；DoP 是 ÷ 2）。
    long m_renderFrames = 0;
    int m_deviceBytes = 0;
    /// 每個聲道一個半緩衝的位元組數。
    std::size_t m_halfBytes = 0;
    bool m_postOutput = false;
    std::array<std::vector<void*>, 2> m_halves; // [半緩衝][聲道]
    std::vector<std::byte> m_scratch;
    /// DoP 退路：DSD 位元流在回呼裡包成 DoP（沿用 DopSink，不配置、不記狀態）。
    DopSink m_dop;
    std::vector<std::byte> m_dopScratch;
    std::vector<std::byte> m_silenceHalf;

    std::atomic<IRenderSource*> m_source{nullptr};
    std::atomic<bool> m_started{false};
    /// 每次 start() 加一，看門狗據此重新起算。
    std::atomic<int> m_startGeneration{0};
    /// 播放中失敗的種類 + 1（0 是沒有失敗）；一旦設定，回呼只送靜音，直到下一次 open()。
    /// 看門狗在 playbackError() 裡設定，所以是 mutable。
    mutable std::atomic<int> m_failure{0};
    std::atomic<bool> m_resetSeen{false};
    std::atomic<long> m_switches{0};
    // 看門狗的狀態：只在輪詢 playbackError() 的執行緒動
    mutable int m_seenGeneration = 0;
    mutable long m_seenSwitches = 0;
    mutable std::chrono::steady_clock::time_point m_seenAt{};
    OutputErrorKind m_lastErrorKind = OutputErrorKind::Other;
};

} // namespace auralis::mod::out
