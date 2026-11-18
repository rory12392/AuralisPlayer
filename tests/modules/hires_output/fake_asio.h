// 假的 ASIO 驅動：實作 IASIO，記下後端的呼叫，由測試代替驅動執行緒呼叫回呼。
#pragma once

#include "asio_drivers.h"

#include <windows.h>

#include <objbase.h>

#include <asiosys.h>

#include <asio.h>
#include <iasiodrv.h>

#include <QThread>

#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

namespace auralis::test {

class FakeAsio final : public IASIO {
public:
    // ---- 測試設定 ----
    long outputs = 2;
    ASIOSampleType sampleType = ASIOSTInt32LSB;
    long preferredFrames = 64;
    bool initOk = true;
    std::set<long> unsupportedRates;
    /// kAsioSetIoFormat(DSD) 是否成功（Spring 3 偶爾回 ASE_NotPresent，T-30）。
    bool dsdSupported = true;
    /// 切到 DSD 後聲道格式是否變成 DSDInt8MSB1（假裝驅動切了格式卻沒改聲道格式時設 false）。
    bool dsdChannelType = true;

    // ---- 記錄 ----
    double rate = 44100;
    ASIOCallbacks* callbacks = nullptr;
    long channels = 0;
    bool started = false;
    bool buffersAlive = false;
    int releases = 0;
    int setRateCalls = 0;
    bool dsdIo = false;          // 目前是否在 DSD 格式
    std::vector<long> ioFormats; // 每次成功的 kAsioSetIoFormat（kASIOPCMFormat／kASIODSDFormat）
    double createdRate = 0;      // 建立緩衝時的取樣率（DSD 時是位元率）
    bool createdDsd = false;                         // 建立緩衝時是否在 DSD 格式
    std::set<Qt::HANDLE> controlThreads;             // 呼叫驅動方法的執行緒
    APTTYPE createApartment = APTTYPE_MTA;           // 建立時那條執行緒的 COM 套間
    std::vector<std::vector<std::uint8_t>> captured; // 每個聲道依序收到的位元組

    /// 代替驅動執行緒：要後端填下一個半緩衝，並把結果接到 captured 後面。
    void tick() {
        callbacks->bufferSwitch(m_next, ASIOTrue);
        for (long ch = 0; ch < channels; ++ch) {
            const auto& half = m_buffers[m_next][ch];
            captured[ch].insert(captured[ch].end(), half.begin(), half.end());
        }
        m_next ^= 1;
    }
    /// 代替驅動送出「要求重設」（Holo 驅動在別的程式接手時送這個）。
    long sendReset() { return callbacks->asioMessage(kAsioResetRequest, 0, nullptr, nullptr); }

    // ---- IUnknown（物件由測試擁有，Release 只計數） ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        note();
        ++releases;
        return 1;
    }

    // ---- IASIO ----
    ASIOBool init(void*) override {
        note();
        return initOk ? ASIOTrue : ASIOFalse;
    }
    void getDriverName(char* name) override { strcpy_s(name, 32, "Fake"); }
    long getDriverVersion() override { return 1; }
    void getErrorMessage(char* text) override { strcpy_s(text, 124, "fake init failed"); }
    ASIOError start() override {
        note();
        started = true;
        return ASE_OK;
    }
    ASIOError stop() override {
        note();
        started = false;
        return ASE_OK;
    }
    ASIOError getChannels(long* in, long* out) override {
        note();
        *in = 0;
        *out = outputs;
        return ASE_OK;
    }
    ASIOError getLatencies(long* in, long* out) override {
        *in = 0;
        *out = preferredFrames;
        return ASE_OK;
    }
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferred, long* gran) override {
        note();
        *minSize = *maxSize = *preferred = preferredFrames;
        *gran = 0;
        return ASE_OK;
    }
    ASIOError canSampleRate(ASIOSampleRate r) override {
        note();
        return unsupportedRates.count(static_cast<long>(r)) ? ASE_NoClock : ASE_OK;
    }
    ASIOError getSampleRate(ASIOSampleRate* r) override {
        *r = rate;
        return ASE_OK;
    }
    ASIOError setSampleRate(ASIOSampleRate r) override {
        note();
        ++setRateCalls;
        if (!dsdIo && r > 1000000) {
            return ASE_NoClock; // Spring 3：PCM 狀態設 DSD 位元率回 ASE_NoClock（T-30）
        }
        rate = r;
        return ASE_OK;
    }
    ASIOError getClockSources(ASIOClockSource*, long* n) override {
        *n = 0;
        return ASE_OK;
    }
    ASIOError setClockSource(long) override { return ASE_OK; }
    ASIOError getSamplePosition(ASIOSamples*, ASIOTimeStamp*) override { return ASE_NotPresent; }
    ASIOError getChannelInfo(ASIOChannelInfo* info) override {
        info->type = dsdIo && dsdChannelType ? ASIOSTDSDInt8MSB1 : sampleType;
        strcpy_s(info->name, sizeof info->name, "Out");
        return ASE_OK;
    }
    ASIOError createBuffers(ASIOBufferInfo* infos, long count, long frames,
                            ASIOCallbacks* cb) override {
        note();
        callbacks = cb;
        channels = count;
        createdRate = rate;
        createdDsd = dsdIo;
        // DSD 的緩衝以位元計：每個位元組 8 個 DSD 位元
        const std::size_t bytes = dsdIo ? static_cast<std::size_t>(frames / 8)
                                        : static_cast<std::size_t>(frames) * sampleBytes();
        m_buffers[0].assign(count, std::vector<std::uint8_t>(bytes, 0xAB));
        m_buffers[1].assign(count, std::vector<std::uint8_t>(bytes, 0xAB));
        captured.assign(count, {});
        for (long i = 0; i < count; ++i) {
            infos[i].buffers[0] = m_buffers[0][i].data();
            infos[i].buffers[1] = m_buffers[1][i].data();
        }
        buffersAlive = true;
        m_next = 0;
        return ASE_OK;
    }
    ASIOError disposeBuffers() override {
        note();
        buffersAlive = false;
        return ASE_OK;
    }
    ASIOError controlPanel() override { return ASE_OK; }
    ASIOError future(long selector, void* params) override {
        if (selector != kAsioSetIoFormat) {
            return ASE_InvalidParameter;
        }
        note();
        const long type = static_cast<ASIOIoFormat*>(params)->FormatType;
        if (type == kASIODSDFormat && !dsdSupported) {
            return ASE_NotPresent;
        }
        dsdIo = type == kASIODSDFormat;
        ioFormats.push_back(type);
        return ASE_SUCCESS;
    }
    ASIOError outputReady() override { return ASE_NotPresent; }

private:
    std::size_t sampleBytes() const {
        return sampleType == ASIOSTInt16LSB ? 2 : sampleType == ASIOSTInt24LSB ? 3 : 4;
    }
    void note() { controlThreads.insert(QThread::currentThreadId()); }

    std::vector<std::vector<std::uint8_t>> m_buffers[2];
    long m_next = 0;
};

/// 假的驅動清單：create 回傳測試擁有的 FakeAsio，並記下建立時的套間。
class FakeAsioDrivers final : public mod::out::AsioDrivers {
public:
    QList<mod::out::AsioDriverEntry> entries;
    FakeAsio* driver = nullptr;

    QList<mod::out::AsioDriverEntry> list() override { return entries; }
    Result<IASIO*> create(const QString&) override {
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        if (SUCCEEDED(CoGetApartmentType(&type, &qualifier))) {
            driver->createApartment = type;
        }
        driver->controlThreads.insert(QThread::currentThreadId());
        return static_cast<IASIO*>(driver);
    }
};

} // namespace auralis::test
