#pragma once

// 已實作的服務搬到自己的標頭；這裡 include 進來，原本只 include 這個檔的程式照樣找得到。
#include <auralis/audio_output.h>
#include <auralis/audio_processor.h>
#include <auralis/cover_service.h>
#include <auralis/dsd.h>
#include <auralis/library_query.h>
#include <auralis/text_decoder.h>

#include <QString>
#include <QStringList>

namespace auralis {

// 這個檔案放還沒實作的核心服務入口（能力合約：入口要先存在，方法由對應的核心任務補上）。
// 核心任務實作某個服務時，把它搬到自己的標頭並補方法；只新增、不改既有方法。
// 這些類別都不能複製；實例由核心擁有，模組經 ModuleHost 取得參考。
// 目前沒有待實作的服務入口了。

// PlaybackService（佇列與播放控制）在 <auralis/playback_service.h>（T-08）。
// CoverService（封面與主色）在 <auralis/cover_service.h>（T-12）。

/// 輸出狀態（路徑、格式、是否位元完美與原因）。欄位由 T-08 定，顯示由 T-13 做。
/// 由 PlaybackService::outputStatusChanged 送出。值型別。
struct OutputStatus {
    /// 輸出路徑（輸出後端的名稱，例：WASAPI 獨占）；沒有在播時為空。
    QString backend;
    /// 音源交出的格式。
    AudioFormat sourceFormat;
    /// 裝置實際開啟的格式。
    AudioFormat deviceFormat;
    bool bitPerfect = true;
    /// 不是位元完美時的原因（例：ReplayGain 已套用）。
    QStringList reasons;
    /// 裝置錯誤的原因（被拔除、被佔用，AC-02-4）；沒有錯誤時為空。
    QString error;

    friend bool operator==(const OutputStatus&, const OutputStatus&) = default;
};

} // namespace auralis
