// T-30 硬體探針的測試訊號：PCM 正弦、DSD 正弦（二階三角積分調變）與 DSD 靜音碼。
// 填緩衝的函式在 ASIO 回呼裡呼叫，不配置記憶體、不丟例外。
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace auralis::mod::out::probe {

// 1 kHz 正弦；振幅以滿刻度為 1。探針一律用小振幅（約 -26 dBFS），避免實機測試太大聲
inline constexpr double kToneHz = 1000.0;
inline constexpr double kToneAmplitude = 0.05;
// DSD 靜音碼（第一個位元在最高位）；第一個位元在最低位時是位元反轉後的 0x96
inline constexpr std::uint8_t kDsdSilenceMsbFirst = 0x69;

class PcmTone {
public:
    PcmTone(double sampleRate, bool silence);
    double next(); // 下一個樣本框的值（兩個聲道相同）

private:
    double m_phase = 0.0;
    double m_step;
    double m_amplitude;
};

// DSD 位元流產生器：每次給 8 個位元（一個位元組），firstBitInMsb 決定位元順序
class DsdTone {
public:
    DsdTone(double dsdRate, bool silence);
    std::uint8_t nextByte(bool firstBitInMsb);

private:
    bool nextBit();
    double m_phase = 0.0;
    double m_step;
    double m_x1 = 0.0;
    double m_x2 = 0.0;
    double m_y = 1.0;
    bool m_silence;
};

// 依 ASIO 樣本格式寫一個 PCM 樣本；不支援的格式回 false
bool writePcmSample(long asioSampleType, void* buffer, long frameIndex, double value);
// ASIO 樣本格式的名稱（給報告用）
std::string sampleTypeName(long asioSampleType);

// 寫一個 2 聲道 DSF 測試檔（給 DoP 經 WASAPI 獨占的實機測試）；dsdRate 是 2822400 或 5644800
bool writeDsfTone(const std::filesystem::path& path, std::uint32_t dsdRate, double seconds,
                  std::string& error);

} // namespace auralis::mod::out::probe
