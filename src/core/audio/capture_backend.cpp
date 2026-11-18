#include "audio/capture_backend.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace auralis::core {

namespace {

qint64 nowTicks() noexcept {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

} // namespace

void ContinuityCheck::begin(const AudioFormat& format) noexcept {
    m_format = format;
    m_havePrevious = false;
    const bool checkable =
        m_threshold > 0.0 && format.rate > 0 && format.channels <= kMaxChannels && format.isValid();
    m_mode = checkable ? Mode::Unknown : Mode::Off;
}

double ContinuityCheck::sampleAt(const std::byte* frame, int channel) const noexcept {
    if (m_format.container == 16) {
        qint16 value = 0;
        std::memcpy(&value, frame + channel * 2, sizeof(value));
        return value / 32768.0;
    }
    if (m_format.isFloat) {
        float value = 0;
        std::memcpy(&value, frame + channel * 4, sizeof(value));
        return value;
    }
    // 24 位元左對齊在 32 位元容器裡，和 32 位元整數同一個刻度
    qint32 value = 0;
    std::memcpy(&value, frame + channel * 4, sizeof(value));
    return value / 2147483648.0;
}

quint8 ContinuityCheck::markerAt(const std::byte* frame, int channel) const noexcept {
    return std::to_integer<quint8>(frame[channel * 4 + 3]);
}

void ContinuityCheck::feed(const std::byte* data, qint64 frames) noexcept {
    if (m_mode == Mode::Off || frames <= 0) {
        return;
    }
    const int channels = m_format.channels;
    const int frameBytes = m_format.bytesPerFrame();
    if (m_mode == Mode::Unknown) {
        // 第一框決定這次開裝置送的是 DoP 還是 PCM
        bool dop = m_format.container == 32 && m_format.bits == 24 && !m_format.isFloat;
        const quint8 first = markerAt(data, 0);
        for (int ch = 0; dop && ch < channels; ++ch) {
            dop = markerAt(data, ch) == first && (first == 0x05 || first == 0xFA) &&
                  std::to_integer<quint8>(data[ch * 4]) == 0;
        }
        m_mode = dop ? Mode::Dop : Mode::Pcm;
    }
    for (qint64 f = 0; f < frames; ++f) {
        const std::byte* frame = data + f * frameBytes;
        if (m_mode == Mode::Dop) {
            const quint8 marker = markerAt(frame, 0);
            bool valid = marker == 0x05 || marker == 0xFA;
            for (int ch = 1; valid && ch < channels; ++ch) {
                valid = markerAt(frame, ch) == marker;
            }
            if (!valid || (m_havePrevious && marker == m_previousMarker)) {
                ++m_dopBreaks;
            }
            m_previousMarker = marker;
        } else {
            for (int ch = 0; ch < channels; ++ch) {
                const double value = sampleAt(frame, ch);
                if (m_havePrevious) {
                    const double step = std::abs(value - m_previous[ch]);
                    m_maxPcmStep = step > m_maxPcmStep ? step : m_maxPcmStep;
                    if (step > m_threshold) {
                        ++m_pcmJumps;
                    }
                }
                m_previous[ch] = value;
            }
        }
        m_havePrevious = true;
    }
}

CaptureBackend::CaptureBackend(Options options)
    : m_options(options), m_continuity(options.continuityThreshold) {}

CaptureBackend::~CaptureBackend() {
    stop();
}

QString CaptureBackend::name() const {
    return QStringLiteral("Capture");
}

Result<AudioFormat> CaptureBackend::open(const AudioFormat& requested) {
    if (!requested.isValid()) {
        return Error{QStringLiteral("格式無效"), 0, QStringLiteral("CaptureBackend::open")};
    }
    close();
    if (m_storage) {
        m_history.append(Session{m_format, captured()});
    }
    m_format = requested;
    // DSD 原生格式沒有 PCM 取樣率：一個樣本框是每聲道 32 個 DSD 位元
    m_framesPerSecond = requested.rate > 0 ? requested.rate : requested.dsdRate / 32;
    m_periodFrames = m_options.periodFrames > 0 ? m_options.periodFrames : m_framesPerSecond / 100;
    m_frameBytes = requested.bytesPerFrame();
    m_storageBytes = m_framesPerSecond * m_options.maxCaptureSeconds * m_frameBytes;
    m_storage = std::make_unique<std::byte[]>(static_cast<std::size_t>(m_storageBytes));
    m_period =
        std::make_unique<std::byte[]>(static_cast<std::size_t>(m_periodFrames * m_frameBytes));
    m_storedBytes = 0;
    m_sourceFrames.store(0, std::memory_order_relaxed);
    m_continuity.begin(requested);
    return requested;
}

Result<bool> CaptureBackend::start(IRenderSource& source) {
    if (!m_period) {
        return Error{QStringLiteral("還沒開啟"), 0, QStringLiteral("CaptureBackend::start")};
    }
    stop();
    m_source = &source;
    m_consumedFrames = 0;
    m_startTicks = nowTicks();
    m_thread.start([this] { return step(); });
    return true;
}

void CaptureBackend::stop() noexcept {
    m_thread.stop();
    m_source = nullptr;
}

void CaptureBackend::close() noexcept {
    stop();
    m_period.reset();
}

QByteArray CaptureBackend::captured() const {
    if (!m_storage) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(m_storage.get()), m_storedBytes);
}

QList<CaptureBackend::Session> CaptureBackend::sessions() const {
    QList<Session> all = m_history;
    if (m_storage) {
        all.append(Session{m_format, captured()});
    }
    return all;
}

bool CaptureBackend::step() noexcept {
    // 即時路徑：照實際經過的時間算出裝置該消耗多少，一次一個裝置緩衝地向來源要
    using Clock = std::chrono::steady_clock;
    const double elapsed =
        std::chrono::duration<double>(Clock::duration(nowTicks() - m_startTicks)).count();
    const auto due = static_cast<qint64>(elapsed * m_options.speed * m_framesPerSecond);
    const qint64 periodBytes = m_periodFrames * m_frameBytes;
    while (m_consumedFrames + m_periodFrames <= due && !m_thread.stopRequested()) {
        const qint64 rendered = m_source->render(m_period.get(), m_periodFrames);
        m_sourceFrames.fetch_add(rendered, std::memory_order_relaxed);
        m_continuity.feed(m_period.get(), m_periodFrames);
        if (m_storedBytes + periodBytes <= m_storageBytes) {
            std::memcpy(m_storage.get() + m_storedBytes, m_period.get(),
                        static_cast<std::size_t>(periodBytes));
            m_storedBytes += periodBytes;
        }
        m_consumedFrames += m_periodFrames;
    }
    // 模擬等待裝置事件（WASAPI 獨占的 WaitForSingleObject）
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return true;
}

} // namespace auralis::core
