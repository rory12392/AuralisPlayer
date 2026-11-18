#include "audio/wasapi_backend.h"

#include "audio/wasapi_format.h"

#include <QCoreApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcWasapi, "auralis.wasapi")

using Microsoft::WRL::ComPtr;

namespace auralis::core {

namespace {

/// 等裝置事件的上限：逾時就問一次裝置狀態，沒有事件的拔除也能發現，也讓 stop() 不用久等。
constexpr DWORD kEventWaitMs = 200;
/// 共享模式的緩衝長度（100 奈秒）：30 毫秒，經混音器時不需要更短。
constexpr REFERENCE_TIME kSharedBuffer = 300000;

QString modeName(WasapiMode mode) {
    return mode == WasapiMode::Exclusive ? QStringLiteral("WASAPI 獨占")
                                         : QStringLiteral("WASAPI 共享");
}

/// 獨占的常見格式（--probe-output 用）：雙聲道、8 種取樣率 × 5 種排列。
QList<AudioFormat> probeMatrix() {
    QList<AudioFormat> formats;
    for (quint32 rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u, 352800u, 384000u}) {
        AudioFormat base;
        base.rate = rate;
        base.channels = 2;
        base.bits = 32;
        base.container = 32;
        base.isFloat = true;
        formats.append(exclusiveCandidates(base));
    }
    return formats;
}

} // namespace

WasapiBackend::WasapiBackend(WasapiMode mode, std::shared_ptr<AudioEndpoints> endpoints,
                             QString deviceId)
    : m_mode(mode), m_endpoints(std::move(endpoints)), m_deviceId(std::move(deviceId)) {}

WasapiBackend::~WasapiBackend() {
    close();
}

QString WasapiBackend::name() const {
    return modeName(m_mode);
}

QString WasapiBackend::signalChange() const {
    // 共享模式經 Windows 混音器（取樣率、音量、混音都可能改變訊號），不是位元完美
    return m_mode == WasapiMode::Shared ? QCoreApplication::translate("WasapiBackend", "共享模式")
                                        : QString();
}

Result<AudioFormat> WasapiBackend::open(const AudioFormat& requested) {
    close();
    m_failure.store(0, std::memory_order_relaxed);
    if (!requested.isValid() || requested.rate == 0) {
        m_lastErrorKind = OutputErrorKind::FormatNotSupported;
        return Error{QStringLiteral("WASAPI 只接受 PCM 格式"), 0,
                     QStringLiteral("WasapiBackend::open")};
    }
    auto opened =
        m_mode == WasapiMode::Exclusive ? openExclusive(requested) : openShared(requested);
    if (!opened) {
        m_lastErrorKind = classifyHresult(opened.error().hresult);
        qCWarning(lcWasapi).noquote()
            << name() << "開啟失敗：" << opened.error().call << opened.error().message;
        close();
        return opened;
    }
    qCInfo(lcWasapi).noquote() << name() << "已開啟" << describeFormat(opened.value()) << "緩衝"
                               << m_bufferFrames << "框";
    return opened;
}

Result<AudioFormat> WasapiBackend::openExclusive(const AudioFormat& requested) {
    // 依序試候選格式：取樣率永遠是來源的，不重取樣（AC-02-2）
    for (const AudioFormat& candidate : exclusiveCandidates(requested)) {
        auto client = m_endpoints->activate(m_deviceId);
        if (!client) {
            return client.error();
        }
        m_client = client.value();
        const WAVEFORMATEXTENSIBLE wave = toWaveFormat(candidate);
        const HRESULT supported =
            m_client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wave.Format, nullptr);
        if (supported == AUDCLNT_E_UNSUPPORTED_FORMAT || supported == S_FALSE) {
            m_client.Reset();
            continue;
        }
        if (FAILED(supported)) {
            return hresultError(supported, "IAudioClient::IsFormatSupported");
        }
        auto initialized = initializeExclusive(wave);
        if (!initialized) {
            if (initialized.error().hresult == AUDCLNT_E_UNSUPPORTED_FORMAT) {
                // 有些驅動程式 IsFormatSupported 說可以、Initialize 才拒絕：換下一個候選
                m_client.Reset();
                continue;
            }
            return initialized.error();
        }
        auto prepared = prepareStream(requested, candidate);
        if (!prepared) {
            return prepared.error();
        }
        return candidate;
    }
    m_client.Reset();
    Error error = hresultError(AUDCLNT_E_UNSUPPORTED_FORMAT, "IAudioClient::IsFormatSupported");
    error.message = QStringLiteral("裝置不支援 %1（獨占模式不重取樣）：%2")
                        .arg(describeFormat(requested), error.message);
    return error;
}

Result<bool> WasapiBackend::initializeExclusive(const WAVEFORMATEXTENSIBLE& wave) {
    REFERENCE_TIME period = 0;
    AURALIS_HR_CHECK(m_client->GetDevicePeriod(&period, nullptr));
    HRESULT hr =
        m_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period,
                             period, &wave.Format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        // 依文件的做法：取對齊後的框數算出週期，換一個新的 IAudioClient 再初始化一次
        UINT32 frames = 0;
        AURALIS_HR_CHECK(m_client->GetBufferSize(&frames));
        period =
            static_cast<REFERENCE_TIME>(10000.0 * 1000 * frames / wave.Format.nSamplesPerSec + 0.5);
        auto client = m_endpoints->activate(m_deviceId);
        if (!client) {
            return client.error();
        }
        m_client = client.value();
        hr = m_client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  period, period, &wave.Format, nullptr);
    }
    if (FAILED(hr)) {
        return hresultError(hr, "IAudioClient::Initialize");
    }
    return true;
}

Result<AudioFormat> WasapiBackend::openShared(const AudioFormat& requested) {
    auto client = m_endpoints->activate(m_deviceId);
    if (!client) {
        return client.error();
    }
    m_client = client.value();
    // 經混音器：讓 Windows 轉成混音格式（含取樣率），所以不是位元完美
    const WAVEFORMATEXTENSIBLE wave = toWaveFormat(requested);
    AURALIS_HR_CHECK(m_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                              AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                          kSharedBuffer, 0, &wave.Format, nullptr));
    auto prepared = prepareStream(requested, requested);
    if (!prepared) {
        return prepared.error();
    }
    return requested;
}

Result<bool> WasapiBackend::prepareStream(const AudioFormat& requested, const AudioFormat& device) {
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_event == nullptr) {
        return hresultError(HRESULT_FROM_WIN32(GetLastError()), "CreateEventW");
    }
    AURALIS_HR_CHECK(m_client->SetEventHandle(m_event));
    AURALIS_HR_CHECK(m_client->GetBufferSize(&m_bufferFrames));
    AURALIS_HR_CHECK(m_client->GetService(
        __uuidof(IAudioRenderClient), reinterpret_cast<void**>(m_render.ReleaseAndGetAddressOf())));
    m_requested = requested;
    m_device = device;
    m_convert = !(requested == device);
    if (m_convert) {
        // 音訊執行緒不配置：打包用的暫存區在這裡配好
        m_scratch = std::make_unique<std::byte[]>(static_cast<std::size_t>(m_bufferFrames) *
                                                  requested.bytesPerFrame());
    }
    return true;
}

Result<bool> WasapiBackend::start(IRenderSource& source) {
    auto started = startStream(source);
    if (!started) {
        m_lastErrorKind = classifyHresult(started.error().hresult);
        m_source = nullptr;
    }
    return started;
}

Result<bool> WasapiBackend::startStream(IRenderSource& source) {
    if (!m_client || !m_render) {
        return Error{QStringLiteral("還沒開啟"), 0, QStringLiteral("WasapiBackend::start")};
    }
    stop();
    m_source = &source;
    // 先填一個緩衝再 Start：獨占模式的第一個事件來時裝置就要有資料
    if (!fill(m_bufferFrames)) {
        return hresultError(m_failure.load(std::memory_order_relaxed),
                            "IAudioRenderClient::GetBuffer");
    }
    AURALIS_HR_CHECK(m_client->Start());
    m_thread.start([this] { return step(); });
    return true;
}

void WasapiBackend::stop() noexcept {
    // 先讓音訊執行緒結束（最多等一次事件逾時），之後才能停裝置、放掉 source
    m_thread.stop();
    if (m_client) {
        m_client->Stop();
        m_client->Reset();
    }
    m_source = nullptr;
}

void WasapiBackend::close() noexcept {
    stop();
    m_render.Reset();
    m_client.Reset();
    if (m_event != nullptr) {
        CloseHandle(m_event);
        m_event = nullptr;
    }
    m_scratch.reset();
    m_bufferFrames = 0;
}

std::optional<OutputError> WasapiBackend::playbackError() const {
    const long hr = m_failure.load(std::memory_order_acquire);
    if (hr == 0) {
        return std::nullopt;
    }
    return OutputError{classifyHresult(hr), hresultError(hr, "WASAPI 音訊執行緒")};
}

bool WasapiBackend::step() noexcept {
    // 即時路徑：不配置、不加鎖、不 I/O、不呼叫 Qt；失敗只記 HRESULT
    const DWORD waited = WaitForSingleObject(m_event, kEventWaitMs);
    if (m_thread.stopRequested()) {
        return false;
    }
    UINT32 padding = 0;
    if (waited != WAIT_OBJECT_0 || m_mode == WasapiMode::Shared) {
        // 逾時時問一次裝置狀態（拔除不一定會發事件）；共享模式本來就要知道還有多少空間
        const HRESULT hr = m_client->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            m_failure.store(hr, std::memory_order_release);
            return false;
        }
        if (waited != WAIT_OBJECT_0) {
            return true;
        }
    }
    // 獨占事件驅動：每個事件要一整個緩衝；共享：補滿剩下的空間
    const UINT32 frames =
        m_mode == WasapiMode::Exclusive ? m_bufferFrames : m_bufferFrames - padding;
    return frames == 0 || fill(frames);
}

bool WasapiBackend::fill(UINT32 frames) noexcept {
    BYTE* data = nullptr;
    HRESULT hr = m_render->GetBuffer(frames, &data);
    if (FAILED(hr)) {
        m_failure.store(hr, std::memory_order_release);
        return false;
    }
    if (m_convert) {
        m_source->render(m_scratch.get(), frames);
        convertSamples(m_scratch.get(), m_requested, reinterpret_cast<std::byte*>(data), m_device,
                       frames);
    } else {
        m_source->render(reinterpret_cast<std::byte*>(data), frames);
    }
    hr = m_render->ReleaseBuffer(frames, 0);
    if (FAILED(hr)) {
        m_failure.store(hr, std::memory_order_release);
        return false;
    }
    return true;
}

WasapiBackendFactory::WasapiBackendFactory(WasapiMode mode,
                                           std::shared_ptr<AudioEndpoints> endpoints)
    : m_mode(mode), m_endpoints(std::move(endpoints)) {}

QString WasapiBackendFactory::name() const {
    return modeName(m_mode);
}

QString WasapiBackendFactory::note() const {
    return m_mode == WasapiMode::Exclusive
               ? QCoreApplication::translate("WasapiBackend", "位元完美")
               : QCoreApplication::translate("WasapiBackend", "經 Windows 混音，非位元完美");
}

QList<OutputDevice> WasapiBackendFactory::devices() {
    auto devices = m_endpoints->list();
    if (!devices) {
        qCWarning(lcWasapi).noquote()
            << "列舉裝置失敗：" << devices.error().call << devices.error().message;
        return {};
    }
    return devices.value();
}

Result<std::unique_ptr<IOutputBackend>> WasapiBackendFactory::create(const QString& deviceId) {
    for (const OutputDevice& device : devices()) {
        if (device.id == deviceId) {
            return std::unique_ptr<IOutputBackend>(
                std::make_unique<WasapiBackend>(m_mode, m_endpoints, deviceId));
        }
    }
    return Error{QStringLiteral("找不到輸出裝置：%1").arg(deviceId), 0,
                 QStringLiteral("WasapiBackendFactory::create")};
}

Result<QStringList> WasapiBackendFactory::probeExclusiveFormats(const QString& deviceId) {
    auto client = m_endpoints->activate(deviceId);
    if (!client) {
        return client.error();
    }
    QStringList supported;
    for (const AudioFormat& format : probeMatrix()) {
        const WAVEFORMATEXTENSIBLE wave = toWaveFormat(format);
        const HRESULT hr =
            client.value()->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wave.Format, nullptr);
        if (hr == S_OK) {
            supported << describeFormat(format);
        } else if (hr != AUDCLNT_E_UNSUPPORTED_FORMAT && hr != S_FALSE) {
            return hresultError(hr, "IAudioClient::IsFormatSupported");
        }
    }
    return supported;
}

} // namespace auralis::core
