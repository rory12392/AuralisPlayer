#include "dsd_mode_controller.h"

#include <auralis/audio_output.h>
#include <auralis/module_settings.h>
#include <auralis/notifier.h>

#include <QFileInfo>
#include <QJSEngine>

#include <array>

namespace auralis::mod::out {

namespace {

DsdModeController* g_instance = nullptr;

const QString kModeKey = QStringLiteral("dsdMode");
const QString kPcmRateKey = QStringLiteral("dsdPcmRate");
const QString kOutputPathKey = QStringLiteral("outputPath");

/// pcmRateLabels 的順序。
constexpr std::array<quint32, 3> kPcmRates{88200, 176400, 352800};

/// 核心在裝置不支援格式而跳過曲目時的原因開頭（WASAPI 與 ASIO 後端的 FormatNotSupported 訊息）。
const QString kUnsupportedPrefix = QStringLiteral("裝置不支援");

} // namespace

DsdModeController::DsdModeController(ModuleSettings& settings, OutputRegistry& outputs,
                                     PlaybackService& playback, Notifier* notifier,
                                     DsdSinkMaker makeSink, bool nativeDsd, QObject* parent)
    : QObject(parent), m_settings(settings), m_outputs(outputs), m_playback(playback),
      m_notifier(notifier), m_makeSink(std::move(makeSink)), m_nativeDsd(nativeDsd) {
    m_mode = dsdModeFromKey(m_settings.value(kModeKey).toString());
    m_pcmRate = validPcmRate(m_settings.value(kPcmRateKey, 176400).toLongLong());
    m_outputPath = m_settings.value(kOutputPathKey).toString();
    connect(&m_playback, &PlaybackService::outputStatusChanged, this, &DsdModeController::onStatus);
    connect(&m_playback, &PlaybackService::trackSkipped, this, &DsdModeController::onSkipped);
    // 直接連線：核心在發出之後才讀註冊的包裝器開新輸出
    connect(&m_playback, &PlaybackService::outputPathChanging, this,
            &DsdModeController::onPathChanging);
    apply();
}

DsdModeController::~DsdModeController() {
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QVariantList DsdModeController::options() const {
    const OutputKind kind = outputKind(m_outputPath);
    const QString pcm = tr("轉 PCM %1").arg(pcmRateLabels().at(pcmRateIndex()));
    const std::array<std::pair<DsdMode, QString>, 3> modes{
        {{DsdMode::Dop, tr("DoP")}, {DsdMode::Native, tr("原生 DSD")}, {DsdMode::Pcm, pcm}}};
    QVariantList list;
    for (const auto& [mode, text] : modes) {
        const QString reason = unavailableReason(mode, kind, m_nativeDsd);
        list.append(QVariantMap{{QStringLiteral("text"), text},
                                {QStringLiteral("enabled"), reason.isEmpty()},
                                {QStringLiteral("reason"), reason}});
    }
    return list;
}

int DsdModeController::pcmRateIndex() const {
    for (std::size_t i = 0; i < kPcmRates.size(); ++i) {
        if (kPcmRates[i] == m_pcmRate) {
            return static_cast<int>(i);
        }
    }
    return 1;
}

QStringList DsdModeController::pcmRateLabels() const {
    return {tr("88.2 kHz"), tr("176.4 kHz"), tr("352.8 kHz")};
}

QString DsdModeController::note() const {
    const DsdMode used = effective();
    if (used == m_mode) {
        return {};
    }
    const QString usedName = used == DsdMode::Pcm ? tr("轉 PCM") : tr("DoP");
    if (m_mode == DsdMode::Native && !m_nativeDsd) {
        return tr("原生 DSD 需要 ASIO 外掛，目前用 %1。").arg(usedName);
    }
    if (m_mode == DsdMode::Native) {
        return tr("ASIO 原生 DSD 要選 ASIO 輸出才能用，目前用 %1。").arg(usedName);
    }
    return tr("WASAPI 共享模式會改動 DoP 封包，目前用 %1。").arg(usedName);
}

void DsdModeController::selectMode(int index) {
    if (index < 0 || index > static_cast<int>(DsdMode::Pcm)) {
        return;
    }
    const auto mode = static_cast<DsdMode>(index);
    if (!unavailableReason(mode, outputKind(m_outputPath), m_nativeDsd).isEmpty()) {
        return;
    }
    if (mode != m_mode) {
        m_mode = mode;
        m_settings.setValue(kModeKey, dsdModeKey(mode));
    }
    setWarning({});
    apply();
    emit changed();
}

void DsdModeController::selectPcmRate(int index) {
    if (index < 0 || index >= static_cast<int>(kPcmRates.size())) {
        return;
    }
    const quint32 rate = kPcmRates[static_cast<std::size_t>(index)];
    if (rate != m_pcmRate) {
        m_pcmRate = rate;
        m_settings.setValue(kPcmRateKey, rate);
        setWarning({});
    }
    apply();
    emit changed();
}

void DsdModeController::setOutputPath(const QString& backendName) {
    if (backendName.isEmpty() || backendName == m_outputPath) {
        return;
    }
    m_outputPath = backendName;
    m_settings.setValue(kOutputPathKey, backendName);
    setWarning({});
    apply();
    emit changed();
}

void DsdModeController::onPathChanging(const QString& backend) {
    // 核心正在換輸出（引擎已停、新輸出還沒開）：只記住路徑、換包裝器，不暫停與重播，
    // 否則會重入播放服務；核心接著用新的包裝器從原位置繼續
    if (backend.isEmpty() || backend == m_outputPath) {
        return;
    }
    m_outputPath = backend;
    m_settings.setValue(kOutputPathKey, backend);
    setWarning({});
    apply(false);
    emit changed();
}

void DsdModeController::onStatus(const OutputStatus& status) {
    // 正在播時的輸出路徑一定是目前選的那個（畫面沒開時也知道）。輸出狀態是核心開始播放的途中
    // 送來的，換包裝器可能要暫停再繼續，所以排到核心做完之後
    if (!status.backend.isEmpty() && status.backend != m_outputPath) {
        QMetaObject::invokeMethod(
            this, [this, backend = status.backend] { setOutputPath(backend); },
            Qt::QueuedConnection);
    }
    if (status.sourceFormat.dsdRate > 0 && status.error.isEmpty()) {
        setWarning({});
    }
    emit changed();
}

void DsdModeController::onSkipped(const TrackRef& track, const QString& reason) {
    if (!isDsdPath(track.path) || !reason.startsWith(kUnsupportedPrefix)) {
        return;
    }
    const QString file = QFileInfo(track.path).fileName();
    const QString suggestion = unsupportedSuggestion(effective());
    setWarning(tr("%1：%2。%3。").arg(file, reason, suggestion));
    if (m_notifier != nullptr) {
        // 核心先送「無法播放…已跳到下一首」，這則排在它之後、取代它，多了改用哪種方式的建議
        const QString text =
            tr("無法播放：%1（%2），已跳到下一首。%3。").arg(file, reason, suggestion);
        QMetaObject::invokeMethod(
            this, [this, text] { m_notifier->notify(text); }, Qt::QueuedConnection);
    }
}

void DsdModeController::apply(bool allowRestart) {
    const DsdMode mode = effective();
    // 轉 PCM 以外的包裝器和取樣率無關
    const quint32 rate = mode == DsdMode::Pcm ? m_pcmRate : 0;
    if (m_registered && mode == m_registeredMode && rate == m_registeredRate) {
        return;
    }
    auto sink = m_makeSink(mode, m_pcmRate);
    // 核心只在開始播放（含暫停後繼續）時把註冊的包裝器交給引擎：DSD 曲目播放中要先停下、
    // 換好再從原位置繼續，新方式才會作用（情況 2）。暫停會記下位置並關裝置，繼續時從那裡重開
    const OutputStatus status = m_playback.outputStatus();
    const bool restart = allowRestart && m_playback.state() == PlaybackService::State::Playing &&
                         status.sourceFormat.dsdRate > 0;
    if (restart) {
        m_playback.pause();
    }
    m_outputs.setDsdSink(std::move(sink));
    m_registered = true;
    m_registeredMode = mode;
    m_registeredRate = rate;
    if (restart) {
        m_playback.play();
    }
}

void DsdModeController::setWarning(const QString& warning) {
    if (m_warning != warning) {
        m_warning = warning;
        emit changed();
    }
}

void DsdModeController::setInstance(DsdModeController* instance) {
    g_instance = instance;
}

DsdModeController* DsdModeController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "DsdModeController", "模組要在載入區塊前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::out
