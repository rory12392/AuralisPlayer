#pragma once

#include "replay_gain.h"

#include <auralis/playback_service.h>

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class ModuleSettings;
} // namespace auralis

namespace auralis::mod::out {

/// ReplayGain 的設定與狀態（輸出面板與設定頁的「ReplayGain」區塊，S-14、S-16；AC-06-1、2、4）。
/// QML 以單例 ReplayGain 取用。模式記在 OUT/replayGain（預設關閉）、防削波記在 OUT/preventClipping
/// （預設開）；改了立刻寫進和處理器共用的原子變數，播放中的這首也立即生效。
/// 狀態說明（status）依處理器替目前這首算的結果與輸出狀態寫：目前的增益與來源、退回音軌增益、
/// 沒有 ReplayGain 資訊、DSD 時不作用。
/// 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class ReplayGainController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ReplayGain)
    QML_SINGLETON
    /// 0 關閉、1 音軌、2 專輯：SegmentedControl 的 currentIndex。
    Q_PROPERTY(int modeIndex READ modeIndex NOTIFY changed)
    /// SegmentedControl 的 model：[{ text }]。
    Q_PROPERTY(QVariantList options READ options CONSTANT)
    Q_PROPERTY(bool preventClipping READ preventClipping NOTIFY changed)
    /// 目前這首的狀態說明；沒有可說的時候空字串。
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    ReplayGainController(ModuleSettings& settings, PlaybackService& playback,
                         std::shared_ptr<RgSettings> shared,
                         std::shared_ptr<const RgAppliedLog> log, QObject* parent = nullptr);
    ~ReplayGainController() override;

    int modeIndex() const;
    QVariantList options() const;
    bool preventClipping() const;
    QString status() const { return m_status; }

    /// 使用者選了第 index 個模式（0 關閉、1 音軌、2 專輯）。
    Q_INVOKABLE void selectMode(int index);
    Q_INVOKABLE void setPreventClipping(bool on);

    static void setInstance(ReplayGainController* instance);
    static ReplayGainController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    /// 依設定、輸出狀態與處理器的紀錄重算狀態說明。
    void refresh();

    ModuleSettings& m_settings;
    PlaybackService& m_playback;
    std::shared_ptr<RgSettings> m_shared;
    std::shared_ptr<const RgAppliedLog> m_log;
    QString m_status;
};

} // namespace auralis::mod::out
