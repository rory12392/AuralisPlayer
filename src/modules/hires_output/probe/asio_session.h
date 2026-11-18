// T-30 硬體探針：列舉 HKLM\SOFTWARE\ASIO、載入驅動、查詢能力、用測試訊號播放。
// T-31（ASIO PCM 後端）與 T-34（ASIO 原生 DSD）接手時可以沿用這裡的列舉與回呼的寫法。
#pragma once

#include <string>
#include <vector>

struct IASIO;

namespace auralis::mod::out::probe {

struct DriverEntry {
    std::wstring key;         // 登錄機碼名稱（使用者看到的驅動名稱）
    std::wstring clsid;       // {…}
    std::wstring description; // Description 值，可能沒有
};

// 列舉 HKLM\SOFTWARE\ASIO；用寬字元版登錄 API，驅動名稱有非 ASCII 字元時也正確
std::vector<DriverEntry> listDrivers();

// 一個已載入、已 init 的驅動；解構時依序 stop、disposeBuffers、Release
class AsioSession {
public:
    AsioSession() = default;
    ~AsioSession();
    AsioSession(const AsioSession&) = delete;
    AsioSession& operator=(const AsioSession&) = delete;

    // 失敗時 error 有原因：CoCreateInstance 的 HRESULT，或 init 回 false 時驅動的 getErrorMessage
    // sysHandle 為空時用主控台視窗；multithreaded=true 時用 MTA 初始化 COM（補查呼叫條件用）
    bool open(const DriverEntry& entry, std::string& error, void* sysHandle = nullptr,
              bool multithreaded = false);
    IASIO* driver() const { return m_driver; }

    // 設定 PCM 或 DSD（kAsioSetIoFormat）；回傳 ASIOFuture 的原始值
    long setIoFormat(long formatType);
    long canDoIoFormat(long formatType);
    long getIoFormat(long& formatType);

    // Pcm：PCM 正弦；Dsd：原生 DSD 位元流（要先 setIoFormat(DSD)）；
    // Dop：DSD 位元流包成 DoP 封包，經 PCM 的 Int32LSB 送出（sampleRate 是 DSD 取樣率的 1/16）
    enum class Signal { Pcm, Dsd, Dop };
    // 用測試訊號播放 seconds 秒（前兩個輸出聲道）；silence=true 時送靜音（PCM 0 或 DSD 靜音碼）。
    // 報告寫進 report（樣本格式、緩衝、延遲、回呼次數、錯誤）
    bool play(double sampleRate, double seconds, Signal signal, bool silence, std::string& report);

private:
    IASIO* m_driver = nullptr;
    bool m_comInitialized = false;
    bool m_buffersCreated = false;
    bool m_started = false;
};

// ASIOError 的名稱（ASE_OK、ASE_NotPresent…）
std::string asioErrorName(long error);

} // namespace auralis::mod::out::probe
