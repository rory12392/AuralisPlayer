// T-30 硬體探針（命令列工具，不進主程式）：
//   auralis_asio_probe list
//   auralis_asio_probe info <驅動名稱片段>
//   auralis_asio_probe dsd-variants <驅動名稱片段>
//   auralis_asio_probe play-pcm <驅動名稱片段> <取樣率> <秒> [silence]
//   auralis_asio_probe play-dsd <驅動名稱片段> <DSD 取樣率> <秒> [silence]
//   auralis_asio_probe play-dop <驅動名稱片段> <DSD 取樣率> <秒> [silence]（DoP 經 ASIO 的 PCM）
//   auralis_asio_probe write-dsf <檔案> <DSD 取樣率> <秒>
// 結束碼：0 成功、1 驅動或播放失敗、2 參數錯誤
#include "asio_session.h"
#include "probe_signal.h"

#include <windows.h>

#include <objbase.h>

#include <iasiodrv.h>

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>

using namespace auralis::mod::out::probe;

namespace {

void print(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

std::string utf8(const std::wstring& text) {
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

std::wstring lower(std::wstring text) {
    for (auto& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

const DriverEntry* findDriver(const std::vector<DriverEntry>& drivers, const std::wstring& part) {
    for (const auto& d : drivers) {
        if (lower(d.key).find(lower(part)) != std::wstring::npos) {
            return &d;
        }
    }
    return nullptr;
}

int commandList() {
    const auto drivers = listDrivers();
    print("HKLM\\SOFTWARE\\ASIO：" + std::to_string(drivers.size()) + " 個驅動\n");
    for (const auto& d : drivers) {
        print("  " + utf8(d.key) + "  " + utf8(d.clsid) + "  " + utf8(d.description) + "\n");
    }
    return 0;
}

void reportRates(IASIO* driver, const std::vector<double>& rates) {
    std::string ok;
    std::string no;
    for (double r : rates) {
        const long result = driver->canSampleRate(r);
        (result == ASE_OK ? ok : no) += std::to_string(static_cast<long long>(r)) + " ";
    }
    print("  支援：" + ok + "\n  不支援：" + no + "\n");
}

void reportChannel0(IASIO* driver) {
    ASIOChannelInfo info{};
    info.channel = 0;
    info.isInput = ASIOFalse;
    const long result = driver->getChannelInfo(&info);
    print("  輸出聲道 0：" + std::string(info.name) + "，樣本格式 " + sampleTypeName(info.type) +
          "（" + asioErrorName(result) + "）\n");
}

int commandInfo(const DriverEntry& entry) {
    AsioSession session;
    std::string error;
    if (!session.open(entry, error)) {
        print("開啟失敗：" + error + "\n");
        return 1;
    }
    IASIO* driver = session.driver();
    char name[128] = {};
    driver->getDriverName(name);
    print("驅動：" + std::string(name) + "，版本 " + std::to_string(driver->getDriverVersion()) +
          "\n");
    long inputs = 0;
    long outputs = 0;
    driver->getChannels(&inputs, &outputs);
    print("聲道：輸入 " + std::to_string(inputs) + "、輸出 " + std::to_string(outputs) + "\n");
    long minSize = 0;
    long maxSize = 0;
    long preferred = 0;
    long granularity = 0;
    driver->getBufferSize(&minSize, &maxSize, &preferred, &granularity);
    print("緩衝（樣本）：最小 " + std::to_string(minSize) + "、最大 " + std::to_string(maxSize) +
          "、建議 " + std::to_string(preferred) + "、粒度 " + std::to_string(granularity) + "\n");
    long inLatency = 0;
    long outLatency = 0;
    const long latencyResult = driver->getLatencies(&inLatency, &outLatency);
    print("延遲（建立緩衝前，樣本）：輸入 " + std::to_string(inLatency) + "、輸出 " +
          std::to_string(outLatency) + "（" + asioErrorName(latencyResult) + "）\n");
    ASIOSampleRate current = 0;
    driver->getSampleRate(&current);
    print("目前取樣率：" + std::to_string(static_cast<long long>(current)) + "\n");

    print("PCM：\n");
    reportChannel0(driver);
    reportRates(driver,
                {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000});
    print("ASIOFuture(kAsioCanReportOverload)：" +
          asioErrorName(driver->future(kAsioCanReportOverload, nullptr)) + "\n");

    long format = 0;
    print("ASIOFuture(kAsioGetIoFormat)：" + asioErrorName(session.getIoFormat(format)) +
          "，格式 " + std::to_string(format) + "\n");
    print("ASIOFuture(kAsioCanDoIoFormat, DSD)：" +
          asioErrorName(session.canDoIoFormat(kASIODSDFormat)) + "\n");
    const long setDsd = session.setIoFormat(kASIODSDFormat);
    print("ASIOFuture(kAsioSetIoFormat, DSD)：" + asioErrorName(setDsd) + "\n");
    if (setDsd == ASE_SUCCESS) {
        print("DSD：\n");
        reportChannel0(driver);
        reportRates(driver, {2822400, 5644800, 11289600, 22579200});
        driver->getBufferSize(&minSize, &maxSize, &preferred, &granularity);
        print("  緩衝（樣本）：最小 " + std::to_string(minSize) + "、最大 " +
              std::to_string(maxSize) + "、建議 " + std::to_string(preferred) + "\n");
        print("切回 PCM：" + asioErrorName(session.setIoFormat(kASIOPCMFormat)) + "\n");
    }
    return 0;
}

// kAsioSetIoFormat(DSD) 失敗時，試幾種呼叫順序，確認是驅動不支援還是順序問題
int commandDsdVariants(const DriverEntry& entry) {
    // 每一種都重新載入驅動：開啟條件 → 事前動作 → kAsioSetIoFormat(DSD)；
    // 成功時列出 DSD 狀態再切回 PCM
    HWND window = CreateWindowExW(0, L"STATIC", L"auralis-asio-probe", WS_OVERLAPPED, 0, 0, 10, 10,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    struct Variant {
        const char* name;
        void* sysHandle;
        bool mta;
        double preRate;  // 先 setSampleRate 的 PCM 取樣率；0 表示不設
        bool prePlay;    // 先用 PCM 靜音跑 0.3 秒（建立、啟動、釋放緩衝）
        bool queryFirst; // 先 kAsioGetIoFormat 與 kAsioCanDoIoFormat
    };
    const Variant variants[] = {
        {"init(主控台視窗) 後立刻設", nullptr, false, 0, false, false},
        {"init(隱藏的頂層視窗) 後立刻設", window, false, 0, false, false},
        {"COM 用 MTA", nullptr, true, 0, false, false},
        {"先查 GetIoFormat／CanDoIoFormat", nullptr, false, 0, false, true},
        {"先 setSampleRate(44100)", nullptr, false, 44100, false, false},
        {"先 setSampleRate(88200)", nullptr, false, 88200, false, false},
        {"先 setSampleRate(176400)", nullptr, false, 176400, false, false},
        {"先 setSampleRate(352800)", nullptr, false, 352800, false, false},
        {"先 setSampleRate(2822400)", nullptr, false, 2822400, false, false},
        {"先播 0.3 秒 PCM 靜音", nullptr, false, 0, true, false},
    };
    for (const auto& v : variants) {
        AsioSession session;
        std::string error;
        if (!session.open(entry, error, v.sysHandle, v.mta)) {
            print(std::string(v.name) + "：開啟失敗：" + error + "\n");
            continue;
        }
        IASIO* driver = session.driver();
        std::string pre;
        if (v.queryFirst) {
            long format = 0;
            pre += "GetIoFormat " + asioErrorName(session.getIoFormat(format)) + "（" +
                   std::to_string(format) + "）、CanDo " +
                   asioErrorName(session.canDoIoFormat(kASIODSDFormat)) + "；";
        }
        if (v.preRate > 0) {
            pre += "setSampleRate " + asioErrorName(driver->setSampleRate(v.preRate)) + "；";
        }
        if (v.prePlay) {
            std::string report;
            pre += std::string("靜音播放 ") +
                   (session.play(44100, 0.3, AsioSession::Signal::Pcm, true, report) ? "成功"
                                                                                     : "失敗") +
                   "；";
        }
        const long set = session.setIoFormat(kASIODSDFormat);
        print(std::string(v.name) + "：" + pre + "SetIoFormat(DSD) " + asioErrorName(set) + "\n");
        if (set == ASE_SUCCESS) {
            long format = 0;
            session.getIoFormat(format);
            print("  GetIoFormat → " + std::to_string(format) + "；canSampleRate(2822400) " +
                  asioErrorName(driver->canSampleRate(2822400)) + "；setSampleRate(2822400) " +
                  asioErrorName(driver->setSampleRate(2822400)) + "\n");
            reportChannel0(driver);
            session.setIoFormat(kASIOPCMFormat);
        }
    }
    if (window) {
        DestroyWindow(window);
    }
    return 0;
}

int commandPlay(const DriverEntry& entry, double rate, double seconds, AsioSession::Signal signal,
                bool silence) {
    const bool dsd = signal == AsioSession::Signal::Dsd;
    AsioSession session;
    std::string error;
    if (!session.open(entry, error)) {
        print("開啟失敗：" + error + "\n");
        return 1;
    }
    const long format = session.setIoFormat(dsd ? kASIODSDFormat : kASIOPCMFormat);
    print(std::string("kAsioSetIoFormat(") + (dsd ? "DSD" : "PCM") + ")：" + asioErrorName(format) +
          "\n");
    if (dsd && format != ASE_SUCCESS) {
        print("此驅動不支援原生 DSD\n");
        return 1;
    }
    std::string report;
    const bool ok = session.play(rate, seconds, signal, silence, report);
    print(report);
    if (dsd) {
        session.setIoFormat(kASIOPCMFormat); // 留給下一個程式的是 PCM 狀態
    }
    print(ok ? "播放完成\n" : "播放失敗\n");
    return ok ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const std::vector<std::wstring> args(argv + 1, argv + argc);
    if (args.empty()) {
        print("用法見原始碼開頭\n");
        return 2;
    }
    const std::wstring& command = args[0];
    if (command == L"list") {
        return commandList();
    }
    if (command == L"write-dsf" && args.size() == 4) {
        std::string error;
        const auto rate = static_cast<std::uint32_t>(std::wcstoul(args[2].c_str(), nullptr, 10));
        if (!writeDsfTone(args[1], rate, std::wcstod(args[3].c_str(), nullptr), error)) {
            print("寫入失敗：" + error + "\n");
            return 1;
        }
        print("已寫入\n");
        return 0;
    }
    if (args.size() < 2) {
        print("缺少驅動名稱\n");
        return 2;
    }
    const auto drivers = listDrivers();
    const DriverEntry* entry = findDriver(drivers, args[1]);
    if (!entry) {
        print("找不到名稱含「" + utf8(args[1]) + "」的 ASIO 驅動\n");
        return 2;
    }
    print("驅動機碼：" + utf8(entry->key) + "\n");
    if (command == L"info") {
        return commandInfo(*entry);
    }
    if (command == L"dsd-variants") {
        return commandDsdVariants(*entry);
    }
    if ((command == L"play-pcm" || command == L"play-dsd" || command == L"play-dop") &&
        args.size() >= 4) {
        double rate = std::wcstod(args[2].c_str(), nullptr);
        const double seconds = std::wcstod(args[3].c_str(), nullptr);
        const bool silence = args.size() >= 5 && args[4] == L"silence";
        auto signal = AsioSession::Signal::Pcm;
        if (command == L"play-dsd") {
            signal = AsioSession::Signal::Dsd;
        } else if (command == L"play-dop") {
            signal = AsioSession::Signal::Dop;
            rate /= 16; // 參數是 DSD 取樣率，DoP 的 PCM 取樣率是它的 1/16
        }
        return commandPlay(*entry, rate, seconds, signal, silence);
    }
    print("參數不對\n");
    return 2;
}
