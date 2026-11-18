#pragma once

#include "smtc_refresh.h"

#include <auralis/result.h>

#include <QByteArray>
#include <QLoggingCategory>

#include <cstdint>
#include <functional>
#include <memory>

namespace auralis::mod::smtc {

Q_DECLARE_LOGGING_CATEGORY(lcSmtc)

/// 系統媒體控制顯示的播放狀態（對應 Windows 的 MediaPlaybackStatus；Closed 表示本程式不再播放）。
enum class SmtcStatus { Closed, Stopped, Playing, Paused };

/// 使用者在系統媒體控制按下的按鈕（鍵盤媒體鍵、音量面板）。Other 是本程式沒有啟用的按鈕。
enum class SmtcButton { Play, Pause, Stop, Next, Previous, Other };

/// 主視窗的系統媒體傳輸控制（SMTC）：取得、啟用按鈕、播放狀態、程式結束時清除。
/// 曲目資訊與封面（DisplayUpdater）也接在這個類別上。
/// WinRT 的程式只在 smtc_session.cpp，這個標頭不引入 WinRT，模組其他檔案照常用 Qt。
/// 執行緒：create()、setStatus()、setTrack()、setThumbnail()、clearTrack()、close() 與解構只在
/// UI 執行緒呼叫；按鈕的回呼在 WinRT 的執行緒
/// 呼叫，呼叫端要自己轉回 UI 執行緒。
class SmtcSession final : public TrackSink {
public:
    using ButtonHandler = std::function<void(SmtcButton)>;

    /// 取得 hwnd 那個視窗的 SMTC，啟用播放、暫停、停止、上一首、下一首，狀態先設成停止。
    /// 失敗（例：視窗不存在、系統不支援）回傳 Error，不丟例外。
    static Result<std::unique_ptr<SmtcSession>> create(std::uintptr_t hwnd, ButtonHandler onButton);

    ~SmtcSession() override;
    SmtcSession(const SmtcSession&) = delete;
    SmtcSession& operator=(const SmtcSession&) = delete;

    void setStatus(SmtcStatus status) noexcept;
    /// 顯示曲名、藝人、專輯（音樂類型）與縮圖（PNG 內容；空的表示不放縮圖），一次更新，
    /// 上一首的縮圖同時換掉（AC-18-2）。失敗只記日誌。
    void setTrack(const QString& title, const QString& artist, const QString& album,
                  const QByteArray& thumbnailPng) noexcept override;
    /// 只換縮圖（封面晚一步讀好時）。失敗只記日誌。
    void setThumbnail(const QByteArray& thumbnailPng) noexcept override;
    /// 清掉曲目資訊與縮圖（佇列沒有曲目時）。失敗只記日誌。
    void clearTrack() noexcept override;
    /// 先撤銷按鈕事件（之後不會再有新的回呼），再把狀態設成 Closed、清掉顯示並停用，
    /// 系統的媒體控制不殘留「播放中」。可重複呼叫；解構時也會呼叫。
    void close() noexcept;

private:
    struct Impl;
    explicit SmtcSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace auralis::mod::smtc
