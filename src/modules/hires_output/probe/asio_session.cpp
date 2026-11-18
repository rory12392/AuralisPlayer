#include "asio_session.h"

#include "probe_signal.h"

#include <windows.h>

#include <objbase.h>

#include <iasiodrv.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>

namespace auralis::mod::out::probe {

namespace {

std::string narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::string hex(long value) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(value));
    return buf;
}

// ASIO 的回呼沒有使用者指標，一個行程只能有一個驅動在跑，所以狀態放在檔案範圍
struct CallbackState {
    IASIO* driver = nullptr;
    ASIOBufferInfo buffers[2]{};
    long bufferSize = 0;
    long sampleType = 0;
    AsioSession::Signal signal = AsioSession::Signal::Pcm;
    long dopFrame = 0;
    bool postOutput = false;
    std::optional<PcmTone> pcm;
    std::optional<DsdTone> dsdTone;
    std::atomic<long> switches{0};
    std::atomic<long> overloads{0};
    std::atomic<long> resetRequests{0};
    std::atomic<bool> unsupportedType{false};
};
CallbackState g_state;

// 即時路徑：不配置記憶體、不加鎖、不 I/O
void fillHalf(long index) {
    void* left = g_state.buffers[0].buffers[index];
    void* right = g_state.buffers[1].buffers[index];
    if (g_state.signal == AsioSession::Signal::Dop) {
        // DoP：每個 32 位元容器 = 標記（0x05／0xFA 交替）＋ 16 個 DSD 位元（先到的在高位）＋ 補 0
        for (long f = 0; f < g_state.bufferSize; ++f) {
            const std::uint32_t marker = (g_state.dopFrame++ & 1) ? 0xFAu : 0x05u;
            const std::uint32_t first = g_state.dsdTone->nextByte(true);
            const std::uint32_t second = g_state.dsdTone->nextByte(true);
            const auto word =
                static_cast<std::int32_t>((marker << 24) | (first << 16) | (second << 8));
            static_cast<std::int32_t*>(left)[f] = word;
            static_cast<std::int32_t*>(right)[f] = word;
        }
        return;
    }
    if (g_state.signal == AsioSession::Signal::Dsd) {
        const bool msbFirst = g_state.sampleType == ASIOSTDSDInt8MSB1;
        const long bytes = g_state.bufferSize / 8;
        for (long i = 0; i < bytes; ++i) {
            const std::uint8_t b = g_state.dsdTone->nextByte(msbFirst);
            static_cast<std::uint8_t*>(left)[i] = b;
            static_cast<std::uint8_t*>(right)[i] = b;
        }
        return;
    }
    for (long f = 0; f < g_state.bufferSize; ++f) {
        const double v = g_state.pcm->next();
        if (!writePcmSample(g_state.sampleType, left, f, v) ||
            !writePcmSample(g_state.sampleType, right, f, v)) {
            g_state.unsupportedType.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

void onBufferSwitch(long index, ASIOBool) {
    fillHalf(index);
    g_state.switches.fetch_add(1, std::memory_order_relaxed);
    if (g_state.postOutput) {
        g_state.driver->outputReady();
    }
}

ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool direct) {
    onBufferSwitch(index, direct);
    return params;
}

void onSampleRateDidChange(ASIOSampleRate) {}

long onAsioMessage(long selector, long value, void*, double*) {
    switch (selector) {
    case kAsioSelectorSupported:
        return (value == kAsioEngineVersion || value == kAsioResetRequest ||
                value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
                value == kAsioSupportsTimeInfo || value == kAsioOverload)
                   ? 1
                   : 0;
    case kAsioEngineVersion:
        return 2;
    case kAsioResetRequest:
        g_state.resetRequests.fetch_add(1, std::memory_order_relaxed);
        return 1;
    case kAsioResyncRequest:
    case kAsioLatenciesChanged:
        return 1;
    case kAsioSupportsTimeInfo:
        return 0; // 用舊式 bufferSwitch 就夠
    case kAsioOverload:
        g_state.overloads.fetch_add(1, std::memory_order_relaxed);
        return 0;
    default:
        return 0;
    }
}

ASIOCallbacks g_callbacks{onBufferSwitch, onSampleRateDidChange, onAsioMessage,
                          onBufferSwitchTimeInfo};

} // namespace

std::string asioErrorName(long error) {
    switch (error) {
    case ASE_OK:
        return "ASE_OK";
    case ASE_SUCCESS:
        return "ASE_SUCCESS";
    case ASE_NotPresent:
        return "ASE_NotPresent";
    case ASE_HWMalfunction:
        return "ASE_HWMalfunction";
    case ASE_InvalidParameter:
        return "ASE_InvalidParameter";
    case ASE_InvalidMode:
        return "ASE_InvalidMode";
    case ASE_SPNotAdvancing:
        return "ASE_SPNotAdvancing";
    case ASE_NoClock:
        return "ASE_NoClock";
    case ASE_NoMemory:
        return "ASE_NoMemory";
    default:
        return hex(error);
    }
}

std::vector<DriverEntry> listDrivers() {
    std::vector<DriverEntry> drivers;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS) {
        return drivers; // 沒有任何 ASIO 驅動：不是錯誤
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD nameLength = 256;
        if (RegEnumKeyExW(root, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
            break;
        }
        DriverEntry entry;
        entry.key = name;
        wchar_t value[512];
        DWORD bytes = sizeof value;
        if (RegGetValueW(root, name, L"CLSID", RRF_RT_REG_SZ, nullptr, value, &bytes) ==
            ERROR_SUCCESS) {
            entry.clsid = value;
        }
        bytes = sizeof value;
        if (RegGetValueW(root, name, L"Description", RRF_RT_REG_SZ, nullptr, value, &bytes) ==
            ERROR_SUCCESS) {
            entry.description = value;
        }
        drivers.push_back(entry);
    }
    RegCloseKey(root);
    return drivers;
}

AsioSession::~AsioSession() {
    if (m_driver) {
        if (m_started) {
            m_driver->stop();
        }
        if (m_buffersCreated) {
            m_driver->disposeBuffers();
        }
        m_driver->Release();
    }
    if (m_comInitialized) {
        CoUninitialize();
    }
}

bool AsioSession::open(const DriverEntry& entry, std::string& error, void* sysHandle,
                       bool multithreaded) {
    const HRESULT co =
        CoInitializeEx(nullptr, multithreaded ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    m_comInitialized = SUCCEEDED(co);
    CLSID clsid{};
    HRESULT hr = CLSIDFromString(entry.clsid.c_str(), &clsid);
    if (FAILED(hr)) {
        error = "CLSID 格式不對：" + narrow(entry.clsid) + "（" + hex(hr) + "）";
        return false;
    }
    // ASIO 的慣例：驅動的 CLSID 同時當介面 ID
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid,
                          reinterpret_cast<void**>(&m_driver));
    if (FAILED(hr)) {
        m_driver = nullptr;
        error = "CoCreateInstance 失敗（" + hex(hr) + "）";
        return false;
    }
    if (!m_driver->init(sysHandle ? sysHandle : GetConsoleWindow())) {
        char message[256] = {};
        m_driver->getErrorMessage(message);
        error = std::string("init 回 false；驅動訊息：「") + message + "」";
        return false;
    }
    return true;
}

long AsioSession::setIoFormat(long formatType) {
    ASIOIoFormat format{};
    format.FormatType = formatType;
    return m_driver->future(kAsioSetIoFormat, &format);
}

long AsioSession::canDoIoFormat(long formatType) {
    ASIOIoFormat format{};
    format.FormatType = formatType;
    return m_driver->future(kAsioCanDoIoFormat, &format);
}

long AsioSession::getIoFormat(long& formatType) {
    ASIOIoFormat format{};
    format.FormatType = kASIOFormatInvalid;
    const long result = m_driver->future(kAsioGetIoFormat, &format);
    formatType = format.FormatType;
    return result;
}

bool AsioSession::play(double sampleRate, double seconds, Signal signal, bool silence,
                       std::string& report) {
    const bool dsd = signal == Signal::Dsd;
    auto line = [&report](const std::string& text) {
        report += text + "\n";
    };
    long inputs = 0;
    long outputs = 0;
    m_driver->getChannels(&inputs, &outputs);
    if (outputs < 2) {
        line("輸出聲道少於 2：" + std::to_string(outputs));
        return false;
    }
    long result = m_driver->canSampleRate(sampleRate);
    line("canSampleRate(" + std::to_string(static_cast<long long>(sampleRate)) + ")：" +
         asioErrorName(result));
    result = m_driver->setSampleRate(sampleRate);
    if (result != ASE_OK) {
        line("setSampleRate 失敗：" + asioErrorName(result));
        return false;
    }
    ASIOChannelInfo info{};
    info.channel = 0;
    info.isInput = ASIOFalse;
    m_driver->getChannelInfo(&info);
    line("輸出聲道 0：" + std::string(info.name) + "，樣本格式 " + sampleTypeName(info.type));
    const bool dsdType = info.type == ASIOSTDSDInt8LSB1 || info.type == ASIOSTDSDInt8MSB1;
    if (dsd != dsdType) {
        line(dsd ? "要送 DSD，但聲道格式不是 1 位元 DSD" : "要送 PCM，但聲道格式是 DSD");
        return false;
    }
    if (signal == Signal::Dop && info.type != ASIOSTInt32LSB) {
        line("DoP 只寫了 Int32LSB 的包法");
        return false;
    }
    long minSize = 0;
    long maxSize = 0;
    long preferred = 0;
    long granularity = 0;
    m_driver->getBufferSize(&minSize, &maxSize, &preferred, &granularity);
    line("緩衝（樣本）：最小 " + std::to_string(minSize) + "、最大 " + std::to_string(maxSize) +
         "、建議 " + std::to_string(preferred) + "、粒度 " + std::to_string(granularity));

    g_state.driver = m_driver;
    g_state.bufferSize = preferred;
    g_state.sampleType = info.type;
    g_state.signal = signal;
    g_state.dopFrame = 0;
    g_state.pcm.emplace(sampleRate, silence);
    g_state.dsdTone.emplace(signal == Signal::Dop ? sampleRate * 16 : sampleRate, silence);
    g_state.switches = 0;
    g_state.overloads = 0;
    g_state.resetRequests = 0;
    g_state.unsupportedType = false;
    for (long ch = 0; ch < 2; ++ch) {
        g_state.buffers[ch] = {};
        g_state.buffers[ch].isInput = ASIOFalse;
        g_state.buffers[ch].channelNum = ch;
    }
    result = m_driver->createBuffers(g_state.buffers, 2, preferred, &g_callbacks);
    if (result != ASE_OK) {
        line("createBuffers 失敗：" + asioErrorName(result));
        return false;
    }
    m_buffersCreated = true;
    long inLatency = 0;
    long outLatency = 0;
    m_driver->getLatencies(&inLatency, &outLatency);
    line("延遲（樣本）：輸入 " + std::to_string(inLatency) + "、輸出 " +
         std::to_string(outLatency) + "（約 " +
         std::to_string(static_cast<int>(outLatency * 1000.0 / sampleRate)) + " ms）");
    g_state.postOutput = m_driver->outputReady() == ASE_OK;
    line(std::string("outputReady：") + (g_state.postOutput ? "支援" : "不支援"));
    fillHalf(0);
    fillHalf(1);

    result = m_driver->start();
    if (result != ASE_OK) {
        line("start 失敗：" + asioErrorName(result));
        return false;
    }
    m_started = true;
    Sleep(static_cast<DWORD>(seconds * 1000.0));
    m_driver->stop();
    m_started = false;
    m_driver->disposeBuffers();
    m_buffersCreated = false;

    const long expected = static_cast<long>(seconds * sampleRate / preferred);
    line("回呼次數 " + std::to_string(g_state.switches.load()) + "（預期約 " +
         std::to_string(expected) + "）；overload " + std::to_string(g_state.overloads.load()) +
         "；reset 要求 " + std::to_string(g_state.resetRequests.load()));
    if (g_state.unsupportedType) {
        line("樣本格式不支援，送出的是未定義的資料");
        return false;
    }
    return g_state.switches.load() > 0;
}

} // namespace auralis::mod::out::probe
