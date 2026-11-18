#include "settings/output_settings.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSettings>

Q_DECLARE_LOGGING_CATEGORY(lcPlayback)

namespace auralis::core {

namespace {

// 鍵名照 <段>/<camelCase>；裝置名稱只用來在裝置不在時告訴使用者是哪一個
const QString kBackendKey = QStringLiteral("core/outputBackend");
const QString kDeviceKey = QStringLiteral("core/outputDevice");
const QString kDeviceNameKey = QStringLiteral("core/outputDeviceName");

} // namespace

PlaybackServiceImpl::OutputChoice savedOutput(const QString& settingsPath) {
    const QSettings settings(settingsPath, QSettings::IniFormat);
    return {settings.value(kBackendKey).toString(), settings.value(kDeviceKey).toString(),
            settings.value(kDeviceNameKey).toString()};
}

Result<bool> selectOutput(PlaybackServiceImpl& playback, const QString& settingsPath,
                          const PlaybackServiceImpl::OutputChoice& choice) {
    Result<bool> switched = playback.switchOutput(choice);
    if (switched) {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue(kBackendKey, choice.backend);
        settings.setValue(kDeviceKey, choice.deviceId);
        settings.setValue(kDeviceNameKey, choice.deviceName);
    }
    return switched;
}

QString restoreOutput(PlaybackServiceImpl& playback, const QString& settingsPath) {
    const PlaybackServiceImpl::OutputChoice saved = savedOutput(settingsPath);
    if (saved.backend.isEmpty()) {
        return {};
    }
    const Result<bool> restored = playback.switchOutput(saved);
    if (restored) {
        return {};
    }
    qCWarning(lcPlayback).noquote() << "記住的輸出不在，改用預設：" << saved.backend
                                    << saved.deviceId << restored.error().message;
    return QCoreApplication::translate("auralis::core::OutputSettings",
                                       "記住的輸出裝置「%1」（%2）不在，已改用預設裝置。")
        .arg(saved.deviceName, saved.backend);
}

} // namespace auralis::core
