#include "settings/volume_settings.h"

#include <QSettings>

namespace auralis::core {

namespace {

// 鍵名照 <段>/<camelCase>
const QString kVolumeKey = QStringLiteral("core/volume");

} // namespace

int savedVolume(const QString& settingsPath) {
    const QSettings settings(settingsPath, QSettings::IniFormat);
    bool ok = false;
    const int volume = settings.value(kVolumeKey, 100).toInt(&ok);
    return ok && volume >= 0 && volume <= 100 ? volume : 100;
}

VolumeSettings::VolumeSettings(PlaybackService& playback, QString settingsPath, QObject* parent)
    : QObject(parent), m_playback(playback), m_settingsPath(std::move(settingsPath)) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSaveDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &VolumeSettings::flush);
    m_playback.setVolume(savedVolume(m_settingsPath));
    // 套用記住的值之後才接：啟動時不用把剛讀到的值再寫回去
    connect(&m_playback, &PlaybackService::volumeChanged, &m_timer, qOverload<>(&QTimer::start));
}

VolumeSettings::~VolumeSettings() {
    if (m_timer.isActive()) {
        flush();
    }
}

void VolumeSettings::flush() {
    m_timer.stop();
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(kVolumeKey, m_playback.volume());
}

} // namespace auralis::core
