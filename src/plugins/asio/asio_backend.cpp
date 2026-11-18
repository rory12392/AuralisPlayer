#include "asio_backend.h"

#include "asio_dsd.h"

#include <windows.h>

#include <objbase.h>

#include <asiosys.h>

#include <asio.h>
#include <iasiodrv.h>

#include <QCoreApplication>
#include <QLoggingCategory>

#include <optional>

Q_LOGGING_CATEGORY(lcAsio, "auralis.out.asio")

namespace auralis::mod::out {

namespace {

// ASIO 的回呼沒有使用者指標：開啟中的後端放在這裡，回呼轉給它（一個行程只能有一個）
std::atomic<AsioBackend*> g_active{nullptr};

void bufferSwitch(long index, ASIOBool) {
    if (AsioBackend* backend = g_active.load(std::memory_order_acquire)) {
        backend->onBufferSwitch(index);
    }
}

ASIOTime* bufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool direct) {
    bufferSwitch(index, direct);
    return params;
}

void sampleRateDidChange(ASIOSampleRate) {}

long asioMessage(long selector, long value, void*, double*) {
    switch (selector) {
    case kAsioSelectorSupported:
        return (value == kAsioEngineVersion || value == kAsioResetRequest ||
                value == kAsioResyncRequest || value == kAsioLatenciesChanged ||
                value == kAsioSupportsTimeInfo)
                   ? 1
                   : 0;
    case kAsioEngineVersion:
        return 2;
    case kAsioResetRequest:
        // 驅動要求重開：Holo 驅動在別的程式接手裝置時送這個，之後不再呼叫回呼
        if (AsioBackend* backend = g_active.load(std::memory_order_acquire)) {
            backend->onResetRequest();
        }
        return 1;
    case kAsioResyncRequest:
    case kAsioLatenciesChanged:
        return 1;
    default:
        return 0; // kAsioSupportsTimeInfo：用舊式 bufferSwitch 就夠
    }
}

ASIOCallbacks g_callbacks{bufferSwitch, sampleRateDidChange, asioMessage, bufferSwitchTimeInfo};

/// 驅動樣本格式的位元組數；不支援的格式（大端序、浮點、DSD、24 位元以外的對齊變體）為 0。
int deviceBytes(ASIOSampleType type) {
    switch (type) {
    case ASIOSTInt16LSB:
        return 2;
    case ASIOSTInt24LSB:
        return 3;
    case ASIOSTInt32LSB:
        return 4;
    default:
        return 0;
    }
}

Error asioError(const QString& message, const char* call) {
    return Error{message, 0, QString::fromLatin1(call)};
}

} // namespace

AsioBackend::AsioBackend(std::shared_ptr<AsioDrivers> drivers, AsioDriverEntry driver,
                         quintptr window, std::chrono::milliseconds stallLimit, AsioNotice notice)
    : m_drivers(std::move(drivers)), m_entry(std::move(driver)), m_window(window),
      m_stallLimit(stallLimit), m_notice(std::move(notice)) {}

AsioBackend::~AsioBackend() {
    close();
}

QString AsioBackend::name() const {
    return QStringLiteral("ASIO");
}

Result<AudioFormat> AsioBackend::open(const AudioFormat& requested) {
    close();
    m_failure.store(0, std::memory_order_release);
    m_resetSeen.store(false, std::memory_order_relaxed);
    m_switches.store(0, std::memory_order_relaxed);
    m_lastErrorKind = OutputErrorKind::Other;
    m_dsdPath = DsdPath::None;
    if (!requested.isValid() || (requested.rate == 0 && !DsdFormat::from(requested).isValid())) {
        m_lastErrorKind = OutputErrorKind::FormatNotSupported;
        return asioError(QStringLiteral("ASIO 輸出只接受 PCM 與 DSD 格式"), "AsioBackend::open");
    }
    std::optional<Result<AudioFormat>> opened;
    m_control.run([&] {
        opened.emplace(openDriver(requested));
        if (!*opened) {
            releaseDriver();
        }
    });
    if (!*opened) {
        qCWarning(lcAsio).noquote()
            << "ASIO 開啟失敗：" << m_entry.name << opened->error().call << opened->error().message;
        return std::move(*opened);
    }
    qCInfo(lcAsio).noquote() << "ASIO 已開啟" << m_entry.name << requested.rate << "Hz"
                             << requested.dsdRate << "DSD" << requested.channels << "ch，緩衝"
                             << m_bufferFrames << "框，每樣本" << m_deviceBytes << "位元組";
    if (m_dsdPath == DsdPath::Dop && m_notice) {
        m_notice(QCoreApplication::translate("AsioBackend", "此驅動不支援原生 DSD，已改用 DoP"));
    }
    return std::move(*opened);
}

Result<AudioFormat> AsioBackend::openDriver(const AudioFormat& requested) {
    const auto unsupported = [this](const QString& message, const char* call) {
        m_lastErrorKind = OutputErrorKind::FormatNotSupported;
        return asioError(message, call);
    };
    AsioBackend* idle = nullptr;
    if (!g_active.compare_exchange_strong(idle, this, std::memory_order_acq_rel)) {
        return asioError(QStringLiteral("已有另一個 ASIO 輸出開啟中"), "AsioBackend::open");
    }
    auto created = m_drivers->create(m_entry.clsid);
    if (!created) {
        return created.error();
    }
    m_driver = created.value();
    const HWND window = m_window != 0 ? reinterpret_cast<HWND>(m_window) : GetDesktopWindow();
    if (!m_driver->init(window)) {
        char message[128] = {};
        m_driver->getErrorMessage(message);
        return asioError(QStringLiteral("ASIO 驅動「%1」無法啟動，裝置可能沒有連接：%2")
                             .arg(m_entry.name, QString::fromLocal8Bit(message)),
                         "IASIO::init");
    }
    long inputs = 0;
    long outputs = 0;
    if (m_driver->getChannels(&inputs, &outputs) != ASE_OK) {
        return asioError(QStringLiteral("無法讀取 ASIO 驅動的聲道數"), "IASIO::getChannels");
    }
    if (outputs < requested.channels) {
        return unsupported(QStringLiteral("裝置只有 %1 個輸出聲道，這首是 %2 聲道")
                               .arg(outputs)
                               .arg(requested.channels),
                           "IASIO::getChannels");
    }
    // 單聲道送到前兩個輸出聲道，不然只有左邊有聲音
    const long used = requested.channels == 1 ? std::min(outputs, 2L) : requested.channels;
    // DSD：先試原生 DSD（AC-04-4）；驅動不接受就在同一個驅動上改送 DoP（AC-04-2），
    // 之後照 PCM 協商 DoP 的取樣率（DSD ÷ 16）與樣本格式
    const DsdFormat dsd = DsdFormat::from(requested);
    AudioFormat pcm = requested;
    if (dsd.isValid()) {
        if (tryNativeDsd(dsd.dsdRate, used)) {
            m_dsdPath = DsdPath::Native;
        } else {
            m_dsdPath = DsdPath::Dop;
            pcm = m_dop.open(dsd).value();
        }
    }
    const bool native = m_dsdPath == DsdPath::Native;
    int bytes = 0;
    if (!native) {
        const auto rate = static_cast<ASIOSampleRate>(pcm.rate);
        if (m_driver->canSampleRate(rate) != ASE_OK) {
            return unsupported(QStringLiteral("裝置不支援 %1 Hz（ASIO 不重取樣）").arg(pcm.rate),
                               "IASIO::canSampleRate");
        }
        ASIOSampleRate current = 0;
        if (m_driver->getSampleRate(&current) != ASE_OK || current != rate) {
            if (m_driver->setSampleRate(rate) != ASE_OK) {
                return unsupported(QStringLiteral("ASIO 驅動無法切到 %1 Hz").arg(pcm.rate),
                                   "IASIO::setSampleRate");
            }
        }
        for (long channel = 0; channel < used; ++channel) {
            ASIOChannelInfo info{};
            info.channel = channel;
            info.isInput = ASIOFalse;
            if (m_driver->getChannelInfo(&info) != ASE_OK) {
                return asioError(QStringLiteral("無法讀取 ASIO 輸出聲道的格式"),
                                 "IASIO::getChannelInfo");
            }
            const int channelBytes = deviceBytes(info.type);
            if (channelBytes == 0 || (bytes != 0 && channelBytes != bytes)) {
                return unsupported(
                    QStringLiteral("ASIO 驅動的樣本格式不支援（類型 %1）").arg(info.type),
                    "IASIO::getChannelInfo");
            }
            bytes = channelBytes;
        }
        if (!pcm.isFloat && pcm.bits > bytes * 8) {
            // 不降位元：位元完美的條件和 WASAPI 獨占相同（AC-03-2）
            return unsupported(QStringLiteral("ASIO 驅動是 %1 位元，放不下 %2 位元的來源")
                                   .arg(bytes * 8)
                                   .arg(pcm.bits),
                               "IASIO::getChannelInfo");
        }
    }
    long minSize = 0;
    long maxSize = 0;
    long preferred = 0;
    long granularity = 0;
    if (m_driver->getBufferSize(&minSize, &maxSize, &preferred, &granularity) != ASE_OK ||
        preferred <= 0) {
        return asioError(QStringLiteral("無法讀取 ASIO 驅動的緩衝大小"), "IASIO::getBufferSize");
    }
    // 原生 DSD 的緩衝以位元計，回呼一次交 ÷ 32 個 DSD 樣本框；DoP 一個 DSD 樣本框變兩個 PCM 框
    const long perRender = native ? 32 : m_dsdPath == DsdPath::Dop ? 2 : 1;
    if (preferred % perRender != 0) {
        return unsupported(
            QStringLiteral("ASIO 驅動的緩衝大小 %1 不能整除 DSD 樣本框").arg(preferred),
            "IASIO::getBufferSize");
    }
    std::vector<ASIOBufferInfo> infos(static_cast<std::size_t>(used));
    for (long channel = 0; channel < used; ++channel) {
        infos[channel].isInput = ASIOFalse;
        infos[channel].channelNum = channel;
    }
    // 回呼用的東西先配好：回呼裡不配置
    m_requested = pcm;
    m_bufferFrames = preferred;
    m_renderFrames = preferred / perRender;
    m_deviceBytes = bytes;
    m_halfBytes = static_cast<std::size_t>(native ? preferred / 8 : preferred * bytes);
    m_scratch.assign(static_cast<std::size_t>(m_renderFrames * requested.bytesPerFrame()),
                     std::byte{0});
    m_dopScratch.assign(
        m_dsdPath == DsdPath::Dop ? static_cast<std::size_t>(preferred * pcm.bytesPerFrame()) : 0,
        std::byte{0});
    prepareSilence();
    const ASIOError buffers = m_driver->createBuffers(infos.data(), used, preferred, &g_callbacks);
    if (buffers != ASE_OK) {
        return asioError(
            QStringLiteral("ASIO 驅動無法建立緩衝（錯誤 %1），裝置可能正在被使用").arg(buffers),
            "IASIO::createBuffers");
    }
    m_buffersCreated = true;
    for (int half = 0; half < 2; ++half) {
        m_halves[half].clear();
        for (const ASIOBufferInfo& info : infos) {
            m_halves[half].push_back(info.buffers[half]);
        }
    }
    m_postOutput = m_driver->outputReady() == ASE_OK;

    if (native) {
        return requested; // 驅動收的就是 DSD 位元流
    }
    // 回報裝置實際收到的：整數來源的有效位元數不變（低位補 0），浮點來源換成驅動的整數；
    // DoP 退路回報 DoP 的 PCM 格式，輸出狀態據此分出「原生」與「DoP」
    AudioFormat device = pcm;
    device.isFloat = false;
    device.container = bytes == 2 ? 16 : 32;
    if (pcm.isFloat) {
        device.bits = static_cast<quint16>(bytes * 8);
    }
    return device;
}

void AsioBackend::releaseDriver() noexcept {
    if (m_driver != nullptr) {
        if (m_running) {
            m_driver->stop();
            m_running = false;
        }
        if (m_buffersCreated) {
            m_driver->disposeBuffers();
            m_buffersCreated = false;
        }
        if (m_ioFormatDsd) {
            // 留給下一首（PCM 或 DoP）與下一個程式的是 PCM 狀態
            setAsioIoFormat(m_driver, false);
            m_ioFormatDsd = false;
        }
        m_driver->Release();
        m_driver = nullptr;
    }
    m_halves[0].clear();
    m_halves[1].clear();
    AsioBackend* self = this;
    g_active.compare_exchange_strong(self, nullptr, std::memory_order_acq_rel);
}

Result<bool> AsioBackend::start(IRenderSource& source) {
    stop();
    std::optional<Result<bool>> started;
    m_control.run([&] {
        if (m_driver == nullptr || !m_buffersCreated) {
            started.emplace(asioError(QStringLiteral("還沒開啟"), "AsioBackend::start"));
            return;
        }
        // 第一次回呼前驅動可能先送出緩衝裡原有的內容
        fillSilence(0);
        fillSilence(1);
        m_source.store(&source, std::memory_order_release);
        m_startGeneration.fetch_add(1, std::memory_order_release);
        m_started.store(true, std::memory_order_release);
        const ASIOError result = m_driver->start();
        if (result != ASE_OK) {
            m_started.store(false, std::memory_order_release);
            m_source.store(nullptr, std::memory_order_release);
            started.emplace(asioError(
                QStringLiteral("ASIO 驅動無法開始播放（錯誤 %1）").arg(result), "IASIO::start"));
            return;
        }
        m_running = true;
        started.emplace(true);
    });
    if (!*started) {
        m_lastErrorKind = OutputErrorKind::Other;
        qCWarning(lcAsio).noquote() << "ASIO 開始播放失敗：" << started->error().message;
    }
    return std::move(*started);
}

void AsioBackend::stop() noexcept {
    // ASIO 規格：stop() 回傳後驅動不再呼叫 bufferSwitch，之後才能放掉 source
    m_control.run([this] {
        if (m_running && m_driver != nullptr) {
            m_driver->stop();
            m_running = false;
        }
    });
    m_started.store(false, std::memory_order_release);
    m_source.store(nullptr, std::memory_order_release);
}

void AsioBackend::close() noexcept {
    stop();
    m_control.run([this] { releaseDriver(); });
}

std::optional<OutputError> AsioBackend::playbackError() const {
    int failure = m_failure.load(std::memory_order_acquire);
    if (failure == 0 && m_started.load(std::memory_order_acquire)) {
        // 看門狗：別的程式開同一個驅動時沒有錯誤碼，只是回呼停了
        const auto now = std::chrono::steady_clock::now();
        const int generation = m_startGeneration.load(std::memory_order_acquire);
        const long switches = m_switches.load(std::memory_order_relaxed);
        if (generation != m_seenGeneration || switches != m_seenSwitches) {
            m_seenGeneration = generation;
            m_seenSwitches = switches;
            m_seenAt = now;
        } else if (now - m_seenAt >= m_stallLimit) {
            fail(OutputErrorKind::DeviceBusy);
            failure = m_failure.load(std::memory_order_acquire);
        }
    }
    if (failure == 0) {
        return std::nullopt;
    }
    const bool reset = m_resetSeen.load(std::memory_order_relaxed);
    return OutputError{
        static_cast<OutputErrorKind>(failure - 1),
        asioError(QStringLiteral("ASIO 驅動被其他程式接手，或裝置已中斷連線（%1）")
                      .arg(reset ? QStringLiteral("驅動要求重設") : QStringLiteral("回呼停止")),
                  "AsioBackend::playbackError")};
}

// onBufferSwitch、fillSilence、prepareSilence（回呼的資料路徑）在 asio_render.cpp

void AsioBackend::onResetRequest() noexcept {
    // 開啟途中（例：換取樣率）驅動也可能要求重設，只有播放中才算被接手
    if (m_started.load(std::memory_order_acquire)) {
        m_resetSeen.store(true, std::memory_order_relaxed);
        fail(OutputErrorKind::DeviceBusy);
    }
}

void AsioBackend::fail(OutputErrorKind kind) const noexcept {
    int none = 0;
    m_failure.compare_exchange_strong(none, static_cast<int>(kind) + 1, std::memory_order_acq_rel);
}

} // namespace auralis::mod::out
