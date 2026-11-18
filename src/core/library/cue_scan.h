#pragma once

#include "library/cue_tracks.h"
#include "library/file_enumerator.h"
#include "library/tag_reader.h"

#include <QSet>
#include <QString>

#include <optional>
#include <vector>

namespace auralis::core {

/// 一個 .cue 在這次掃描的狀況。
struct CuePlan {
    size_t fileIndex = 0;         ///< .cue 本身在 Enumeration::files 的索引
    std::optional<LoadedCue> cue; ///< 讀得到時有值
    QString error;                ///< 讀不到、解析失敗、找不到音訊檔時的原因
    bool duplicate = false; ///< 指向的音訊檔已被前面的 .cue 用掉，這個 .cue 不收錄
    /// 增量比對用的修改時間：.cue 與它指向的音訊檔中最新的那個（換了整軌檔也會重讀）。
    qint64 mtimeNs = 0;
};

struct CueScanPlan {
    std::vector<CuePlan> cues;
    /// 被收錄的 .cue 涵蓋的音訊檔（path_key）：這些檔案不另外列成整檔曲目（AC-07-1）。
    QSet<QString> coveredAudio;
};

/// 讀這次列舉到的每個 .cue（每次掃描都讀，.cue 很小），決定哪些音訊檔由 .cue 分軌。
/// 讀不到的 .cue 不涵蓋任何音訊檔，那些檔案照常以整檔收錄。
/// 同一個音訊檔被兩個 .cue 指到時，列舉順序在前的那個收錄。
/// 執行緒：掃描的工作執行緒。
CueScanPlan planCues(const Enumeration& listing);

/// 一軌 CUE 曲目要寫進 tracks 的資料。
struct CueTrackRow {
    quint32 subsong = 0;
    qint64 startCdFrame = 0;
    qint64 endCdFrame = -1; ///< -1：到音訊檔結尾（寫成 NULL）
    TagData tags;
};

/// 讀 .cue 指向的音訊檔的屬性，組出每一軌的曲目資料：曲名、藝人（軌的 PERFORMER，
/// 沒有時用整張的）、專輯（TITLE）、專輯藝人（整張的 PERFORMER）、軌號、REM 的 GENRE、
/// DATE、DISCNUMBER；.cue 沒寫的欄位與音訊屬性取自音訊檔的標籤。
/// 音訊檔讀不到時回傳 Error。執行緒：掃描的工作執行緒。
Result<std::vector<CueTrackRow>> readCueTracks(const QString& cuePath, const LoadedCue& cue);

} // namespace auralis::core
