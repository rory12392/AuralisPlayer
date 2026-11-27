// PlaybackService：建立、輸出後端、開始播放、輪詢引擎與狀態。其他部分各自一個檔：
// 佇列操作 playback_service_queue.cpp、傳輸控制 playback_service_transport.cpp、
// 開檔與下一首提供者 playback_service_tracks.cpp、輸出切換 playback_service_output.cpp。
#include "playback/playback_service_impl.h"

#include <QLoggingCategory>

#include <algorithm>
#include <random>

Q_LOGGING_CATEGORY(lcPlayback, "auralis.playback")

namespace auralis::core {

namespace {

/// 位置訊號的間隔（毫秒）。
constexpr int kPollMs = 50;

} // namespace

PlaybackServiceImpl::PlaybackServiceImpl(SourceRegistryImpl& sources, OutputRegistryImpl& outputs,
                                         ProcessorChainImpl& processors, QObject* parent)
    : PlaybackServiceImpl(sources, outputs, processors, std::random_device{}(), parent) {}

PlaybackServiceImpl::PlaybackServiceImpl(SourceRegistryImpl& sources, OutputRegistryImpl& outputs,
                                         ProcessorChainImpl& processors, quint32 seed,
                                         QObject* parent)
    : PlaybackService(parent), m_sources(sources), m_outputs(outputs), m_queue(seed),
      m_engine(processors) {
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackServiceImpl::poll);
    m_starter = std::thread([this] { starterLoop(); });
}

PlaybackServiceImpl::~PlaybackServiceImpl() {
    {
        // 關程式：還沒開始的要求丟掉，做到一半的在下一個檢查點放棄；
        // 開檔本身不能中斷，所以最多等一次開檔
        const std::lock_guard lock(m_startMutex);
        m_quit = true;
        m_job.reset();
        ++m_startSeq;
    }
    m_startWake.notify_all();
    m_starter.join();
    m_engine.stop();
}

void PlaybackServiceImpl::setOutputBackend(std::unique_ptr<IOutputBackend> backend) {
    stop();
    // 背景執行緒可能還拿著原本的後端：等它停好才換
    waitForStart();
    m_backend = std::move(backend);
    m_choice = {};
}

IOutputBackend* PlaybackServiceImpl::ensureBackend() {
    if (m_backend) {
        return m_backend.get();
    }
    // 輸出選擇（輸出面板）之前的預設：第一個輸出路徑（WASAPI 獨占）的第一個裝置
    for (const auto& factory : m_outputs.factories()) {
        const QList<OutputDevice> devices = factory->devices();
        if (devices.isEmpty()) {
            continue;
        }
        auto created = factory->create(devices.constFirst().id);
        if (created) {
            m_backend = std::move(created).value();
            m_choice = {factory->name(), devices.constFirst().id, devices.constFirst().name};
            emit outputPathChanging(m_choice.backend);
            return m_backend.get();
        }
        qCWarning(lcPlayback).noquote() << "建立輸出失敗：" << created.error().message;
    }
    return nullptr;
}

// ---- 開始、輪詢、結束（UI 執行緒） ----

void PlaybackServiceImpl::markUnplayable(const QueueEntry& entry, const QString& reason) {
    qCWarning(lcPlayback).noquote() << "無法播放：" << entry.track.path << reason;
    m_queue.setUnplayable(entry.id, true);
    emit trackSkipped(entry.track, reason);
}

void PlaybackServiceImpl::startEntry(std::optional<QueueEntry> entry, qint64 offsetMs) {
    m_startTries = m_queue.entries().size();
    requestStart(std::move(entry), offsetMs);
}

void PlaybackServiceImpl::requestStart(std::optional<QueueEntry> entry, qint64 offsetMs) {
    m_timer.stop();
    if (!entry || m_startTries < 0) {
        stop();
        emit queueChanged();
        return;
    }
    // 選預設輸出（會發 outputPathChanging，模組在這裡換 DSD 包裝器）留在 UI 執行緒，之後才讀包裝器
    IOutputBackend* backend = ensureBackend();
    if (!backend) {
        stopWithError(tr("沒有可用的輸出裝置"));
        return;
    }
    m_pendingEntry = entry;
    m_pendingOffset = offsetMs;
    StartJob job;
    job.entry = std::move(entry);
    job.offsetMs = offsetMs;
    job.backend = backend;
    job.dsdSink = m_outputs.dsdSink();
    postJob(std::move(job));
}

void PlaybackServiceImpl::skipAndContinue(const QueueEntry& entry, const QString& reason) {
    markUnplayable(entry, reason);
    --m_startTries;
    requestStart(m_queue.nextAfter(entry.id, true), 0);
}

void PlaybackServiceImpl::finishStart(const StartResult& result) {
    if (result.seq != m_startSeq.load()) {
        return; // 之後又送了要求：這個結果作廢，引擎由後來的要求處理
    }
    m_starting = false;
    m_pendingEntry.reset();
    if (!result.entry) {
        return; // 只停止
    }
    const QueueEntry& entry = *result.entry;
    if (!m_queue.entry(entry.id)) {
        // 開檔期間這首被移出佇列（清空佇列時它還不是目前曲目）
        stop();
        emit queueChanged();
        return;
    }
    if (result.openError) {
        skipAndContinue(entry, result.openError->message);
        return;
    }
    if (result.startError) {
        if (result.errorKind == OutputErrorKind::FormatNotSupported || result.dsdRejected) {
            // 裝置不支援這個格式，或 DSD 包裝器和這個輸出路徑不相容：跳過這首（AC-02-2）
            skipAndContinue(entry, result.startError->message);
            return;
        }
        stopWithError(deviceErrorText(result.errorKind, *result.startError));
        return;
    }
    const TokenInfo info = tokenInfo(result.token).value_or(TokenInfo{});
    m_token = result.token;
    m_sourceRate = info.sourceRate;
    m_durationMs = info.durationMs;
    m_outputRate = framesPerSecond(result.outputFormat);
    m_outputError.clear();
    m_queue.setCurrent(entry.id);
    setState(State::Playing);
    emit trackChanged();
    emit queueChanged();
    setPosition(result.offsetMs);
    updateOutputStatus();
    m_timer.start();
}

void PlaybackServiceImpl::poll() {
    if (m_state != State::Playing || m_starting) {
        return;
    }
    if (const auto failure = m_engine.outputError()) {
        // 裝置被拔除或被佔用（AC-02-4）：停止並留下原因，裝置恢復後可以再按播放
        stopWithError(deviceErrorText(failure->kind, failure->error));
        return;
    }
    const PlaybackEngine::Position position = m_engine.position();
    if (position.token != m_token) {
        onTokenChanged(position.token);
        if (m_token != position.token) {
            return; // 照佇列重來了
        }
    }
    if (m_outputRate > 0) {
        setPosition(position.frames * 1000 / m_outputRate);
    }
    updateOutputStatus();
    if (m_engine.isFinished()) {
        onFinished();
    }
}

void PlaybackServiceImpl::onTokenChanged(quint64 token) {
    const auto info = tokenInfo(token);
    const auto current = m_queue.current();
    const auto expected = current ? m_queue.nextAfter(current->id, true) : std::nullopt;
    if (!info || !expected || expected->id != info->entryId) {
        // 已經寫進緩衝、來不及撤回的下一首和現在的佇列不符（使用者在最後約 0.5 秒改了佇列）：
        // 照佇列重來
        startEntry(expected);
        return;
    }
    m_token = token;
    m_sourceRate = info->sourceRate;
    m_durationMs = info->durationMs;
    m_outputRate = framesPerSecond(m_engine.status().outputFormat);
    m_queue.setCurrent(info->entryId);
    {
        // 聽過的 token 不會再用到（token 只增不減）
        const std::lock_guard lock(m_tokenMutex);
        std::erase_if(m_tokens, [token](const auto& item) { return item.first < token; });
    }
    emit trackChanged();
    emit queueChanged();
}

void PlaybackServiceImpl::onFinished() {
    const auto error = m_engine.decodeError();
    if (!error) {
        const auto current = m_queue.current();
        startEntry(current ? m_queue.nextAfter(current->id, true) : std::nullopt);
        return;
    }
    // 解碼中途失敗，或換格式時重開裝置失敗（position().token 是開不了的那首）
    const OutputErrorKind kind = m_backend ? m_backend->lastErrorKind() : OutputErrorKind::Other;
    if (kind == OutputErrorKind::DeviceBusy || kind == OutputErrorKind::DeviceLost) {
        stopWithError(deviceErrorText(kind, *error));
        return;
    }
    const auto info = tokenInfo(m_engine.position().token);
    const auto failed = info ? m_queue.entry(info->entryId) : std::nullopt;
    if (!failed) {
        stop();
        return;
    }
    markUnplayable(*failed, error->message);
    startEntry(m_queue.nextAfter(failed->id, true));
}

void PlaybackServiceImpl::stopEngine() {
    m_timer.stop();
    m_pendingEntry.reset();
    if (m_starting) {
        // 引擎在背景執行緒手上：送一個只停止的要求，手上的開始也在下一個檢查點放棄，UI 不等
        postJob(StartJob{});
        return;
    }
    m_engine.stop();
}

void PlaybackServiceImpl::stopWithError(const QString& error) {
    qCWarning(lcPlayback).noquote() << "播放停止：" << error;
    stopEngine();
    m_outputError = error;
    setState(State::Stopped);
    updateOutputStatus();
}

void PlaybackServiceImpl::setState(State state) {
    if (state != m_state) {
        m_state = state;
        emit stateChanged(state);
    }
}

void PlaybackServiceImpl::setPosition(qint64 positionMs) {
    m_positionMs = positionMs;
    emit positionChanged(positionMs);
}

void PlaybackServiceImpl::updateOutputStatus() {
    OutputStatus status;
    bool hasVolume = false;
    bool volumeAvailable = true;
    if (!m_starting && m_engine.isPlaying()) {
        const PlaybackEngine::Status engine = m_engine.status();
        status.backend = engine.backend;
        status.sourceFormat = engine.sourceFormat;
        status.deviceFormat = engine.deviceFormat;
        status.bitPerfect = engine.bitPerfect;
        status.reasons = engine.reasons;
        hasVolume = !engine.volumeReason.isEmpty();
        volumeAvailable = engine.volumeAvailable;
    } else if (m_state == State::Paused) {
        status = m_output; // 暫停時裝置關著，顯示暫停前的狀態；只有音量那一項跟著現在的設定
        volumeAvailable = m_volumeAvailable;
        if (m_outputHasVolume) {
            status.reasons.removeLast();
        }
        hasVolume = volumeAvailable && m_engine.volume() != 100;
        if (hasVolume) {
            status.reasons.append(volumeReasonText(m_engine.volume()));
        }
        status.bitPerfect = status.reasons.isEmpty();
    }
    m_outputHasVolume = hasVolume;
    status.error = m_outputError;
    if (!(status == m_output)) {
        m_output = status;
        emit outputStatusChanged(status);
    }
    if (volumeAvailable != m_volumeAvailable) {
        m_volumeAvailable = volumeAvailable;
        emit volumeAvailableChanged(volumeAvailable);
    }
}

void PlaybackServiceImpl::setVolume(int percent) {
    const int clamped = std::clamp(percent, 0, 100);
    if (clamped == m_engine.volume()) {
        return;
    }
    m_engine.setVolume(clamped);
    emit volumeChanged(clamped);
    // 輸出狀態的「音量 n%」立刻跟上，不等下一次輪詢
    updateOutputStatus();
}

QString PlaybackServiceImpl::volumeUnavailableReason() const {
    return m_volumeAvailable ? QString() : tr("DSD 直通時無法調整音量");
}

} // namespace auralis::core
