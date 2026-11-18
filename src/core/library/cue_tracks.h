#pragma once

#include "audio/next_track.h"
#include "library/cue_sheet.h"

#include <auralis/audio_source.h>
#include <auralis/library_query.h>
#include <auralis/result.h>

#include <QString>

#include <functional>
#include <vector>

namespace auralis::core {

/// 讀好的 .cue：解析結果加上每個 FILE 對應到磁碟上的音訊檔。
struct LoadedCue {
    CueSheet sheet;
    /// 和 sheet.files 同順序的完整路徑（Windows 分隔符）。
    std::vector<QString> audioPaths;
    QString encoding; ///< TextDecoder 判斷出的編碼（日誌用）
};

/// 路徑的副檔名是不是 .cue（不分大小寫）。CUE 曲目的身分是 (.cue 的路徑, 軌號)。
bool isCuePath(const QString& path);

/// 讀 .cue（TextDecoder 判斷編碼，AC-07-3）、解析，並找出每個 FILE 的音訊檔：
/// 先找寫的檔名（相對於 .cue 所在的資料夾，也接受指到該資料夾內的絕對路徑）；不存在時找同資料夾、
/// 同主檔名、副檔名在音樂庫收錄範圍的檔案（常見：.cue 寫 .wav，實際是轉好的 .flac）。
/// FILE 只能指到 .cue 所在資料夾（含子資料夾）內：網路路徑、裝置路徑、跳出資料夾的 ..、資料夾外的
/// 絕對路徑在查詢檔案系統之前就拒絕。.cue 所在資料夾底下到該檔之間有符號連結或 junction
/// （含檔案本身）時也拒絕，連結另一邊不查詢。
/// 被拒、找不到音訊檔、檔案不是音樂庫收錄的格式、或解析失敗時回傳 Error（訊息給掃描報告）。
/// 只讀檔，不改任何東西。執行緒：任何非 UI 執行緒。
Result<LoadedCue> loadCue(const QString& cuePath);

/// CD 影格 → 音源的樣本框（每秒框數是取樣率；DSD 是 DSD 率 ÷ 32，一框是每聲道 32 位元）。
/// 44.1、48、88.2、96、176.4、192 kHz 與 DSD 都能整除，所以沒有誤差。
qint64 cdFramesToSourceFrames(qint64 cdFrames, const AudioFormat& format);

/// 依曲目身分開好播放引擎要播的一首：整檔（subsong 0）從頭播到尾；CUE 曲目重新讀 .cue，
/// 開它指向的音訊檔，區段是這一軌的 INDEX 01 到下一軌的 INDEX 01（AC-07-2）。
/// 相鄰的 CUE 曲目接續播放時，交給 NextTrackProvider 的就是這裡開好的曲目，
/// 引擎依區段讀，中間不會有空白。token 原樣放進 EngineTrack。
/// 執行緒：解碼執行緒或其他非 UI 執行緒（會讀檔、開音源）。
Result<EngineTrack> openTrack(const TrackRef& track, const SourceRegistry& sources,
                              quint64 token = 0);

/// 測試用：loadCue 每次要查詢檔案系統（FILE 指向的檔案在不在）之前呼叫，參數是要查的路徑；
/// 回傳 false 時當成不存在、不去查。用來證明被拒的路徑一次都沒有被查詢或開啟。
/// 正式程式不設定；只能在沒有掃描或播放進行時設定或清除（傳空的 std::function）。
void setCueProbeHookForTest(std::function<bool(const QString&)> hook);

} // namespace auralis::core
