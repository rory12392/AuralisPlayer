#include "fake_wasapi.h"

#include <chrono>
#include <cstring>

namespace auralis::test {

HRESULT FakeAudioClient::QueryInterface(REFIID riid, void** out) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioClient)) {
        *out = static_cast<IAudioClient*>(this);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

bool FakeAudioClient::lost() const {
    return !m_device->present.load();
}

HRESULT FakeAudioClient::Initialize(AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME duration,
                                    REFERENCE_TIME period, const WAVEFORMATEX* format, LPCGUID) {
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (m_initialized) {
        return AUDCLNT_E_ALREADY_INITIALIZED;
    }
    WAVEFORMATEXTENSIBLE wave{};
    std::memcpy(&wave, format, sizeof(WAVEFORMATEX) + format->cbSize);
    {
        std::lock_guard lock(m_device->mutex);
        m_device->initialized.push_back(wave);
        m_device->modes.push_back(mode);
        m_device->flags.push_back(flags);
        m_device->periods.push_back(period);
    }
    if (FAILED(m_device->initResult)) {
        return m_device->initResult;
    }
    const DWORD rate = format->nSamplesPerSec;
    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
        if (m_device->supports && !m_device->supports(wave)) {
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        if (m_device->alignedFrames != 0 && !m_device->alignmentReported) {
            m_device->alignmentReported = true;
            m_bufferFrames = m_device->alignedFrames;
            return AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED;
        }
        m_bufferFrames = static_cast<UINT32>((rate * period + 5000000) / 10000000);
    } else {
        m_bufferFrames =
            static_cast<UINT32>(duration > 0 ? rate * duration / 10000000 : rate / 100);
    }
    m_blockAlign = format->nBlockAlign;
    m_buffer.assign(static_cast<std::size_t>(m_bufferFrames) * m_blockAlign, std::byte{0});
    m_initialized = true;
    return S_OK;
}

HRESULT FakeAudioClient::GetBufferSize(UINT32* frames) {
    if (m_bufferFrames == 0) {
        return AUDCLNT_E_NOT_INITIALIZED;
    }
    *frames = m_bufferFrames;
    return S_OK;
}

HRESULT FakeAudioClient::GetStreamLatency(REFERENCE_TIME* latency) {
    *latency = 0;
    return S_OK;
}

HRESULT FakeAudioClient::GetCurrentPadding(UINT32* padding) {
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    *padding = 0;
    return S_OK;
}

HRESULT FakeAudioClient::IsFormatSupported(AUDCLNT_SHAREMODE mode, const WAVEFORMATEX* format,
                                           WAVEFORMATEX** closest) {
    if (closest != nullptr) {
        *closest = nullptr;
    }
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    WAVEFORMATEXTENSIBLE wave{};
    std::memcpy(&wave, format, sizeof(WAVEFORMATEX) + format->cbSize);
    if (mode == AUDCLNT_SHAREMODE_EXCLUSIVE && m_device->supports && !m_device->supports(wave)) {
        return AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    return S_OK;
}

HRESULT FakeAudioClient::GetMixFormat(WAVEFORMATEX** format) {
    *format = nullptr;
    return E_NOTIMPL;
}

HRESULT FakeAudioClient::GetDevicePeriod(REFERENCE_TIME* defaultPeriod,
                                         REFERENCE_TIME* minimumPeriod) {
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (defaultPeriod != nullptr) {
        *defaultPeriod = 100000; // 10 毫秒
    }
    if (minimumPeriod != nullptr) {
        *minimumPeriod = 30000;
    }
    return S_OK;
}

HRESULT FakeAudioClient::Start() {
    if (!m_initialized || m_event == nullptr) {
        return AUDCLNT_E_NOT_INITIALIZED;
    }
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    m_running = true;
    m_pump = std::thread([this] {
        // 模擬裝置：每隔一段時間要求下一個緩衝
        while (m_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_device->eventIntervalMs));
            SetEvent(m_event);
        }
    });
    return S_OK;
}

HRESULT FakeAudioClient::Stop() {
    m_running = false;
    if (m_pump.joinable()) {
        m_pump.join();
    }
    return S_OK;
}

HRESULT FakeAudioClient::SetEventHandle(HANDLE event) {
    if (!m_initialized) {
        return AUDCLNT_E_NOT_INITIALIZED;
    }
    m_event = event;
    return S_OK;
}

HRESULT FakeAudioClient::GetService(REFIID riid, void** out) {
    if (riid == __uuidof(IAudioRenderClient)) {
        *out = static_cast<IAudioRenderClient*>(this);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT FakeAudioClient::GetBuffer(UINT32 frames, BYTE** data) {
    if (lost()) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
    if (frames > m_bufferFrames) {
        return AUDCLNT_E_BUFFER_TOO_LARGE;
    }
    *data = reinterpret_cast<BYTE*>(m_buffer.data());
    return S_OK;
}

HRESULT FakeAudioClient::ReleaseBuffer(UINT32 frames, DWORD) {
    const std::size_t bytes = static_cast<std::size_t>(frames) * m_blockAlign;
    const std::size_t at = m_device->writtenBytes.load();
    if (at + bytes <= m_device->written.size()) {
        std::memcpy(m_device->written.data() + at, m_buffer.data(), bytes);
        m_device->writtenBytes.store(at + bytes);
    }
    const int buffers = ++m_device->buffers;
    if (m_device->unplugAfterBuffers >= 0 && buffers >= m_device->unplugAfterBuffers) {
        m_device->present = false;
    }
    return S_OK;
}

Result<QList<OutputDevice>> FakeEndpoints::list() {
    if (!m_device->present.load()) {
        return QList<OutputDevice>{};
    }
    return QList<OutputDevice>{OutputDevice{m_device->id, m_device->name}};
}

Result<Microsoft::WRL::ComPtr<IAudioClient>> FakeEndpoints::activate(const QString& id) {
    if (id != m_device->id || !m_device->present.load()) {
        return hresultError(AUDCLNT_E_DEVICE_INVALIDATED, "FakeEndpoints::activate");
    }
    {
        std::lock_guard lock(m_device->mutex);
        ++m_device->activations;
    }
    m_clients.push_back(std::make_unique<FakeAudioClient>(m_device));
    return Microsoft::WRL::ComPtr<IAudioClient>(static_cast<IAudioClient*>(m_clients.back().get()));
}

std::function<bool(const WAVEFORMATEXTENSIBLE&)>
supportsOnly(std::vector<std::tuple<DWORD, WORD, WORD>> formats) {
    return [formats = std::move(formats)](const WAVEFORMATEXTENSIBLE& wave) {
        for (const auto& [rate, container, valid] : formats) {
            if (wave.Format.nSamplesPerSec == rate && wave.Format.wBitsPerSample == container &&
                wave.Samples.wValidBitsPerSample == valid && wave.SubFormat.Data1 == 1) {
                return true;
            }
        }
        return false;
    };
}

} // namespace auralis::test
