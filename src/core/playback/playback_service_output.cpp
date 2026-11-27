// PlaybackService：輸出面板的切換（AC-02-6）與裝置錯誤的文字（AC-02-4）。
#include "playback/playback_service_impl.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcPlayback)

namespace auralis::core {

Result<bool> PlaybackServiceImpl::switchOutput(const OutputChoice& choice) {
    const auto factory = m_outputs.find(choice.backend);
    if (!factory) {
        return Error{tr("找不到輸出路徑：%1").arg(choice.backend), 0,
                     QStringLiteral("PlaybackServiceImpl::switchOutput")};
    }
    auto created = factory->create(choice.deviceId);
    if (!created) {
        qCWarning(lcPlayback).noquote() << "建立輸出失敗：" << created.error().message;
        return Error{tr("找不到這個輸出裝置"), 0,
                     QStringLiteral("PlaybackServiceImpl::switchOutput")};
    }
    // 還在開檔時以要開的那首與位置為準
    const bool resume = m_state == State::Playing || m_pendingEntry.has_value();
    const qint64 at = m_pendingEntry ? m_pendingOffset : m_positionMs;
    const auto current = m_pendingEntry ? m_pendingEntry : m_queue.current();
    stopEngine();
    // 換輸出要同步回報成敗（設定頁與輸出面板照回傳值顯示），而且背景執行緒停好才能換後端，
    // 所以這裡等開始播放做完（換輸出是少數操作，開檔的等待留在這裡）
    waitForStart();
    auto previous = std::move(m_backend);
    const OutputChoice previousChoice = m_choice;
    m_backend = std::move(created).value();
    m_choice = choice;
    // 換了裝置，之前那個裝置的錯誤不再適用
    m_outputError.clear();
    // 引擎已停：讓模組在新輸出開啟前換好 DSD 包裝器（startEntry 再讀註冊的包裝器）
    emit outputPathChanging(choice.backend);
    if (!resume || !current) {
        updateOutputStatus();
        return true;
    }
    startEntry(current, at);
    waitForStart();
    if (m_state == State::Playing || m_outputError.isEmpty() || !previous) {
        // 成功；或新裝置不支援這些曲目的格式而照 AC-02-2 跳過（不是裝置錯誤）
        return true;
    }
    // 新裝置開不了（被佔用、不見了）：換回原本的輸出、從原位置繼續播（畫面規格 S-14）
    const QString failure = m_outputError;
    m_backend = std::move(previous);
    m_choice = previousChoice;
    m_outputError.clear();
    emit outputPathChanging(previousChoice.backend);
    startEntry(current, at);
    waitForStart();
    return Error{failure, 0, QStringLiteral("PlaybackServiceImpl::switchOutput")};
}

QString PlaybackServiceImpl::deviceErrorText(OutputErrorKind kind, const Error& error) const {
    qCWarning(lcPlayback).noquote() << "輸出裝置錯誤：" << error.call << error.message;
    switch (kind) {
    case OutputErrorKind::DeviceBusy:
        return tr("裝置被其他程式使用中");
    case OutputErrorKind::DeviceLost:
        return tr("輸出裝置已中斷連線");
    case OutputErrorKind::Other:
    case OutputErrorKind::FormatNotSupported:
        break;
    }
    // 有 HRESULT 的是系統錯誤碼的說明，不給使用者看；其他是核心自己寫的中文原因
    return error.hresult != 0 ? tr("無法開啟輸出裝置") : error.message;
}

} // namespace auralis::core
