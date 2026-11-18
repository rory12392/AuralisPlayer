#include "probe_signal.h"

#include <windows.h>

#include <objbase.h>

#include <iasiodrv.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <vector>

namespace auralis::mod::out::probe {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::uint8_t reverseBits(std::uint8_t b) {
    std::uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        r = static_cast<std::uint8_t>((r << 1) | ((b >> i) & 1));
    }
    return r;
}

void putLe(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

void putTag(std::vector<std::uint8_t>& out, const char (&tag)[5]) {
    out.insert(out.end(), tag, tag + 4);
}

} // namespace

PcmTone::PcmTone(double sampleRate, bool silence)
    : m_step(kTwoPi * kToneHz / sampleRate), m_amplitude(silence ? 0.0 : kToneAmplitude) {}

double PcmTone::next() {
    const double value = m_amplitude * std::sin(m_phase);
    m_phase += m_step;
    if (m_phase >= kTwoPi) {
        m_phase -= kTwoPi;
    }
    return value;
}

DsdTone::DsdTone(double dsdRate, bool silence)
    : m_step(kTwoPi * kToneHz / dsdRate), m_silence(silence) {}

bool DsdTone::nextBit() {
    // 二階三角積分調變：振幅很小，兩個積分器不會發散
    const double input = kToneAmplitude * std::sin(m_phase);
    m_phase += m_step;
    if (m_phase >= kTwoPi) {
        m_phase -= kTwoPi;
    }
    m_x1 += input - m_y;
    m_x2 += m_x1 - m_y;
    m_y = m_x2 >= 0.0 ? 1.0 : -1.0;
    return m_y > 0.0;
}

std::uint8_t DsdTone::nextByte(bool firstBitInMsb) {
    if (m_silence) {
        return firstBitInMsb ? kDsdSilenceMsbFirst : reverseBits(kDsdSilenceMsbFirst);
    }
    std::uint8_t byte = 0;
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t bit = nextBit() ? 1 : 0;
        if (firstBitInMsb) {
            byte = static_cast<std::uint8_t>(byte | (bit << (7 - i)));
        } else {
            byte = static_cast<std::uint8_t>(byte | (bit << i));
        }
    }
    return byte;
}

bool writePcmSample(long asioSampleType, void* buffer, long frameIndex, double value) {
    const double clamped = value > 1.0 ? 1.0 : (value < -1.0 ? -1.0 : value);
    switch (asioSampleType) {
    case ASIOSTInt16LSB: {
        static_cast<std::int16_t*>(buffer)[frameIndex] =
            static_cast<std::int16_t>(std::lround(clamped * 32767.0));
        return true;
    }
    case ASIOSTInt24LSB: {
        const auto v = static_cast<std::int32_t>(std::lround(clamped * 8388607.0));
        auto* p = static_cast<std::uint8_t*>(buffer) + 3 * static_cast<std::ptrdiff_t>(frameIndex);
        p[0] = static_cast<std::uint8_t>(v);
        p[1] = static_cast<std::uint8_t>(v >> 8);
        p[2] = static_cast<std::uint8_t>(v >> 16);
        return true;
    }
    case ASIOSTInt32LSB: {
        static_cast<std::int32_t*>(buffer)[frameIndex] =
            static_cast<std::int32_t>(std::llround(clamped * 2147483647.0));
        return true;
    }
    case ASIOSTInt32LSB24: {
        static_cast<std::int32_t*>(buffer)[frameIndex] =
            static_cast<std::int32_t>(std::lround(clamped * 8388607.0));
        return true;
    }
    case ASIOSTFloat32LSB: {
        static_cast<float*>(buffer)[frameIndex] = static_cast<float>(clamped);
        return true;
    }
    default:
        return false;
    }
}

std::string sampleTypeName(long asioSampleType) {
    switch (asioSampleType) {
    case ASIOSTInt16LSB:
        return "Int16LSB";
    case ASIOSTInt24LSB:
        return "Int24LSB";
    case ASIOSTInt32LSB:
        return "Int32LSB";
    case ASIOSTFloat32LSB:
        return "Float32LSB";
    case ASIOSTFloat64LSB:
        return "Float64LSB";
    case ASIOSTInt32LSB16:
        return "Int32LSB16";
    case ASIOSTInt32LSB18:
        return "Int32LSB18";
    case ASIOSTInt32LSB20:
        return "Int32LSB20";
    case ASIOSTInt32LSB24:
        return "Int32LSB24";
    case ASIOSTDSDInt8LSB1:
        return "DSDInt8LSB1";
    case ASIOSTDSDInt8MSB1:
        return "DSDInt8MSB1";
    case ASIOSTDSDInt8NER8:
        return "DSDInt8NER8";
    default:
        return "其他（" + std::to_string(asioSampleType) + "）";
    }
}

bool writeDsfTone(const std::filesystem::path& path, std::uint32_t dsdRate, double seconds,
                  std::string& error) {
    constexpr std::uint32_t kBlock = 4096;
    constexpr std::uint32_t kChannels = 2;
    const auto bitsPerChannel = static_cast<std::uint64_t>(dsdRate * seconds);
    const std::uint64_t blocks = (bitsPerChannel / 8 + kBlock - 1) / kBlock;
    const std::uint64_t bytesPerChannel = blocks * kBlock;
    const std::uint64_t dataBytes = bytesPerChannel * kChannels;
    const std::uint64_t fileSize = 28 + 52 + 12 + dataBytes;

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(fileSize));
    putTag(out, "DSD ");
    putLe(out, 28, 8);
    putLe(out, fileSize, 8);
    putLe(out, 0, 8); // 沒有中繼資料
    putTag(out, "fmt ");
    putLe(out, 52, 8);
    putLe(out, 1, 4);         // 格式版本
    putLe(out, 0, 4);         // DSD raw
    putLe(out, 2, 4);         // 立體聲
    putLe(out, kChannels, 4); // 聲道數
    putLe(out, dsdRate, 4);   // 取樣率
    putLe(out, 1, 4);         // 每樣本 1 位元（第一個位元在最低位）
    putLe(out, bytesPerChannel * 8, 8);
    putLe(out, kBlock, 4);
    putLe(out, 0, 4);
    putTag(out, "data");
    putLe(out, 12 + dataBytes, 8);

    DsdTone tone(dsdRate, false);
    std::vector<std::uint8_t> block(kBlock);
    for (std::uint64_t b = 0; b < blocks; ++b) {
        for (auto& byte : block) {
            byte = tone.nextByte(false);
        }
        // 兩個聲道同一個訊號：每聲道各一個 4096 位元組的區塊交錯
        for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
            out.insert(out.end(), block.begin(), block.end());
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "無法建立檔案";
        return false;
    }
    file.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file) {
        error = "寫入失敗";
        return false;
    }
    return true;
}

} // namespace auralis::mod::out::probe
