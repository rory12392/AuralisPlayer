#include "shell/player_controller.h"

#include <auralis/cover_service.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QLocale>
#include <QQmlEngine>
#include <QVariantMap>

namespace auralis::ui {

namespace {

PlayerController* g_instance = nullptr;

QString trackKey(const TrackRef& track) {
    return track.path + QLatin1Char('\n') + QString::number(track.subsong);
}

QVariantMap readout(const QString& label, const QString& value, const QString& unit = {}) {
    // 不知道的值寫「—」，不帶單位
    if (value.isEmpty()) {
        return {{QStringLiteral("label"), label},
                {QStringLiteral("value"), QStringLiteral("—")},
                {QStringLiteral("unit"), QString()}};
    }
    return {{QStringLiteral("label"), label},
            {QStringLiteral("value"), value},
            {QStringLiteral("unit"), unit}};
}

/// 數量用千分位（介面規範 §6.1）。
QString grouped(int value) {
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value);
}

/// 44100 → 44.1、96000 → 96。
QString kiloHertz(int rate) {
    return rate % 1000 == 0 ? QString::number(rate / 1000) : QString::number(rate / 1000.0, 'f', 1);
}

} // namespace

NowPlayingInfo nowPlayingFrom(const TrackInfo& track, int trackTotal, qint64 fileBytes) {
    NowPlayingInfo info;
    info.album = track.album;
    info.albumId = track.albumId;
    info.year = track.year;
    info.trackNo = track.trackNo;
    info.trackTotal = trackTotal;
    info.codec = track.codec;
    info.sampleRate = track.sampleRate;
    info.bits = track.bits;
    info.dsdRate = track.dsdRate;
    info.coverKey = track.coverKey;
    if (fileBytes > 0 && track.durationMs > 0) {
        // 位元組 × 8 ÷ 毫秒 ＝ kbps
        info.bitrateKbps = static_cast<int>(fileBytes * 8 / track.durationMs);
    }
    return info;
}

PlayerController::PlayerController(PlaybackService& service, QueueModel& queue,
                                   NowPlayingLookup lookup, QObject* parent)
    : QObject(parent), m_service(service), m_queue(queue), m_lookup(std::move(lookup)) {
    // 查詢依序做，不和掃描搶執行緒
    m_pool.setMaxThreadCount(1);
    connect(&m_service, &PlaybackService::trackChanged, this, &PlayerController::requestNowPlaying);
    connect(&m_service, &PlaybackService::queueChanged, this, &PlayerController::requestNowPlaying);
    // DSD 的輸出方式跟著輸出狀態
    connect(&m_service, &PlaybackService::outputStatusChanged, this,
            &PlayerController::nowPlayingChanged);
    // 目前曲目會因換曲、佇列改變（移掉正在播的那首）或查到曲目資訊而改變
    connect(&m_service, &PlaybackService::trackChanged, this, &PlayerController::trackChanged);
    connect(&m_service, &PlaybackService::queueChanged, this, &PlayerController::trackChanged);
    connect(&m_queue, &QueueModel::metaChanged, this, &PlayerController::trackChanged);
    connect(&m_service, &PlaybackService::stateChanged, this, &PlayerController::stateChanged);
    // 已停止看狀態、目前曲目與裝置錯誤（錯誤在 stateChanged 之後才寫進輸出狀態）
    connect(&m_service, &PlaybackService::stateChanged, this, &PlayerController::stoppedChanged);
    connect(&m_service, &PlaybackService::outputStatusChanged, this,
            &PlayerController::stoppedChanged);
    connect(&m_service, &PlaybackService::queueChanged, this, &PlayerController::stoppedChanged);
    connect(&m_service, &PlaybackService::positionChanged, this,
            &PlayerController::positionChanged);
    connect(&m_service, &PlaybackService::repeatModeChanged, this,
            &PlayerController::repeatModeChanged);
    connect(&m_service, &PlaybackService::shuffleChanged, this, &PlayerController::shuffleChanged);
    connect(&m_service, &PlaybackService::volumeChanged, this, &PlayerController::volumeChanged);
    connect(&m_service, &PlaybackService::volumeAvailableChanged, this,
            &PlayerController::volumeAvailableChanged);
    requestNowPlaying();
}

PlayerController::~PlayerController() {
    m_pool.waitForDone();
}

QString PlayerController::coverUrl() const {
    return m_info ? CoverService::imageUrl(m_info->coverKey) : QString();
}

QString PlayerController::albumLine() const {
    if (!m_info) {
        return {};
    }
    const QString year = m_info->year > 0 ? QString::number(m_info->year) : QString();
    if (m_info->album.isEmpty() || year.isEmpty()) {
        return m_info->album + year;
    }
    return m_info->album + QStringLiteral("　") + year;
}

QString PlayerController::trackText() const {
    if (!m_info || m_info->trackNo <= 0) {
        return {};
    }
    return m_info->trackTotal > 0 ? tr("曲目 %1 / %2").arg(m_info->trackNo).arg(m_info->trackTotal)
                                  : tr("曲目 %1").arg(m_info->trackNo);
}

QVariantList PlayerController::readouts() const {
    const NowPlayingInfo info = m_info.value_or(NowPlayingInfo{});
    const QString bitrate = info.bitrateKbps > 0 ? grouped(info.bitrateKbps) : QString();
    if (info.dsdRate > 0) {
        return {readout(tr("格式"), info.codec),
                readout(tr("DSD 倍率"), QStringLiteral("DSD%1").arg(info.dsdRate / 44100)),
                readout(tr("輸出方式"), dsdOutputMode()),
                readout(tr("位元率"), bitrate, QStringLiteral("kbps"))};
    }
    return {readout(tr("格式"), info.codec),
            readout(tr("取樣率"), info.sampleRate > 0 ? kiloHertz(info.sampleRate) : QString(),
                    QStringLiteral("kHz")),
            readout(tr("位元深度"), info.bits > 0 ? QString::number(info.bits) : QString(),
                    QStringLiteral("bit")),
            readout(tr("位元率"), bitrate, QStringLiteral("kbps"))};
}

QString PlayerController::dsdOutputMode() const {
    const OutputStatus status = m_service.outputStatus();
    if (status.backend.isEmpty() || status.sourceFormat.dsdRate == 0) {
        return {};
    }
    if (status.deviceFormat.rate == 0 && status.deviceFormat.dsdRate > 0) {
        return tr("原生 DSD");
    }
    // 轉 PCM 的包裝器把「DSD 轉 PCM」放進非位元完美的原因；否則就是 DoP 封包
    return status.reasons.contains(QStringLiteral("DSD 轉 PCM")) ? tr("轉 PCM") : tr("DoP");
}

void PlayerController::requestNowPlaying() {
    const auto current = m_service.currentEntry();
    const QString key = current ? trackKey(current->track) : QString();
    if (key == m_infoKey) {
        return;
    }
    m_infoKey = key;
    m_info.reset();
    emit nowPlayingChanged();
    if (!current || !m_lookup) {
        return;
    }
    // 查音樂庫是 I/O，不在 UI 執行緒做（Architecture §5）
    m_pool.start([this, key, track = current->track, lookup = m_lookup] {
        const auto found = lookup(track);
        QMetaObject::invokeMethod(
            this,
            [this, key, found] {
                // 查的時候已經換曲就丟掉
                if (key == m_infoKey) {
                    m_info = found;
                    emit nowPlayingChanged();
                }
            },
            Qt::QueuedConnection);
    });
}

void PlayerController::waitForLookups() {
    m_pool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

qint64 PlayerController::durationMs() const {
    const qint64 known = m_service.durationMs();
    if (known >= 0) {
        return known;
    }
    const auto current = m_service.currentEntry();
    return current ? m_queue.meta(current->track).durationMs : -1;
}

QString PlayerController::title() const {
    const auto current = m_service.currentEntry();
    return current ? m_queue.meta(current->track).title : QString();
}

QString PlayerController::artist() const {
    const auto current = m_service.currentEntry();
    return current ? m_queue.meta(current->track).artist : QString();
}

QString PlayerController::repeatMode() const {
    switch (m_service.repeatMode()) {
    case PlaybackService::RepeatMode::All:
        return QStringLiteral("all");
    case PlaybackService::RepeatMode::One:
        return QStringLiteral("one");
    case PlaybackService::RepeatMode::Off:
        break;
    }
    return QStringLiteral("off");
}

bool PlayerController::playTracks(const QString& json) {
    return m_queue.withTracks(
        json, this, [this](const QList<TrackRef>& tracks) { m_service.playFrom(tracks, 0); });
}

bool PlayerController::enqueueNext(const QString& json) {
    return m_queue.withTracks(
        json, this, [this](const QList<TrackRef>& tracks) { m_service.enqueueNext(tracks); });
}

bool PlayerController::enqueueLast(const QString& json) {
    return m_queue.withTracks(
        json, this, [this](const QList<TrackRef>& tracks) { m_service.enqueueLast(tracks); });
}

void PlayerController::togglePause() {
    // 停止時 togglePause 也從目前曲目（或佇列第一首）開始播
    if (m_service.state() == PlaybackService::State::Stopped) {
        m_service.play();
    } else {
        m_service.togglePause();
    }
}

bool PlayerController::stopped() const {
    return m_service.state() == PlaybackService::State::Stopped && hasTrack() &&
           m_service.outputStatus().error.isEmpty();
}

void PlayerController::cycleRepeat() {
    using Mode = PlaybackService::RepeatMode;
    const Mode mode = m_service.repeatMode();
    m_service.setRepeatMode(mode == Mode::Off   ? Mode::All
                            : mode == Mode::All ? Mode::One
                                                : Mode::Off);
}

QString PlayerController::formatTime(qint64 ms) const {
    if (ms < 0) {
        return {};
    }
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = totalSeconds / 60 % 60;
    const qint64 seconds = totalSeconds % 60;
    const QString ss = QStringLiteral("%1").arg(seconds, 2, 10, QLatin1Char('0'));
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(ss);
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(ss);
}

void PlayerController::setInstance(PlayerController* instance) {
    g_instance = instance;
}

PlayerController* PlayerController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PlayerController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui
