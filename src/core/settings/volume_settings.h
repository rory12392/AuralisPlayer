#pragma once

#include <auralis/playback_service.h>

#include <QObject>
#include <QString>
#include <QTimer>

namespace auralis::core {

/// 記住的音量（settings.ini 的 core/volume，0–100）；沒有記過或值不合法時是 100。
int savedVolume(const QString& settingsPath);

/// 音量記在 settings.ini 的 core/ 段，重新啟動後沿用（AC-02-7）。
/// 建立時把記住的音量套用到 playback；之後 playback 的音量一改就記下來（拖曳滑桿時會連續改，
/// 所以停下來 kSaveDelayMs 之後才寫檔一次，解構時還沒寫的也會寫）。
/// 執行緒：UI 執行緒。playback 要活得比這個物件久。
class VolumeSettings final : public QObject {
    Q_OBJECT

public:
    /// 最後一次改變之後隔多久寫檔（毫秒）。
    static constexpr int kSaveDelayMs = 500;

    VolumeSettings(PlaybackService& playback, QString settingsPath, QObject* parent = nullptr);
    ~VolumeSettings() override;

    /// 還沒寫的現在就寫（測試與解構時用）。
    void flush();

private:
    PlaybackService& m_playback;
    QString m_settingsPath;
    QTimer m_timer;
};

} // namespace auralis::core
