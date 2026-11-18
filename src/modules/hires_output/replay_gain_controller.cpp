#include "replay_gain_controller.h"

#include "dsd_mode.h"

#include <auralis/module_settings.h>

#include <QJSEngine>
#include <QVariantMap>

#include <utility>

namespace auralis::mod::out {

namespace {

ReplayGainController* g_instance = nullptr;

const QString kModeKey = QStringLiteral("replayGain");
const QString kClipKey = QStringLiteral("preventClipping");

RgMode modeFromIndex(int index) {
    switch (index) {
    case 1:
        return RgMode::Track;
    case 2:
        return RgMode::Album;
    default:
        break;
    }
    return RgMode::Off;
}

} // namespace

ReplayGainController::ReplayGainController(ModuleSettings& settings, PlaybackService& playback,
                                           std::shared_ptr<RgSettings> shared,
                                           std::shared_ptr<const RgAppliedLog> log, QObject* parent)
    : QObject(parent), m_settings(settings), m_playback(playback), m_shared(std::move(shared)),
      m_log(std::move(log)) {
    m_shared->mode.store(rgModeFromKey(m_settings.value(kModeKey).toString()));
    m_shared->preventClipping.store(m_settings.value(kClipKey, true).toBool());
    connect(&m_playback, &PlaybackService::trackChanged, this, &ReplayGainController::refresh);
    connect(&m_playback, &PlaybackService::outputStatusChanged, this,
            &ReplayGainController::refresh);
    refresh();
}

ReplayGainController::~ReplayGainController() {
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

int ReplayGainController::modeIndex() const {
    switch (m_shared->mode.load()) {
    case RgMode::Track:
        return 1;
    case RgMode::Album:
        return 2;
    case RgMode::Off:
        break;
    }
    return 0;
}

QVariantList ReplayGainController::options() const {
    QVariantList list;
    for (const QString& text : {tr("關閉"), tr("音軌"), tr("專輯")}) {
        list.append(QVariantMap{{QStringLiteral("text"), text}});
    }
    return list;
}

bool ReplayGainController::preventClipping() const {
    return m_shared->preventClipping.load();
}

void ReplayGainController::selectMode(int index) {
    if (index < 0 || index > 2) {
        return;
    }
    const RgMode mode = modeFromIndex(index);
    m_settings.setValue(kModeKey, rgModeKey(mode));
    m_shared->mode.store(mode);
    refresh();
    emit changed();
}

void ReplayGainController::setPreventClipping(bool on) {
    m_settings.setValue(kClipKey, on);
    m_shared->preventClipping.store(on);
    refresh();
    emit changed();
}

void ReplayGainController::refresh() {
    std::optional<RgTrackGain> gain;
    if (const auto entry = m_playback.currentEntry(); entry && m_log) {
        gain = m_log->find(entry->track);
    }
    const QString status = rgStatusText(m_shared->mode.load(), m_shared->preventClipping.load(),
                                        dsdRouteLabel(m_playback.outputStatus()), gain);
    if (status != m_status) {
        m_status = status;
        emit changed();
    }
}

void ReplayGainController::setInstance(ReplayGainController* instance) {
    g_instance = instance;
}

ReplayGainController* ReplayGainController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "ReplayGainController", "模組要在載入區塊前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::out
