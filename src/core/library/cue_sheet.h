#pragma once

#include <auralis/result.h>

#include <QHash>
#include <QString>

#include <vector>

namespace auralis::core {

/// 一個 CD 影格是 1/75 秒；.cue 的時間 MM:SS:FF 換算成影格數。
constexpr qint64 kCdFramesPerSecond = 75;

/// .cue 的一軌（只收 AUDIO 軌）。
struct CueTrack {
    int number = 0;    ///< TRACK 的軌號，也是曲目身分的 subsong
    int fileIndex = 0; ///< 所屬的 FILE（CueSheet::files 的索引）
    qint64 start = 0;  ///< INDEX 01（CD 影格，相對於所屬的音訊檔）
    qint64 end = -1; ///< 下一軌的 INDEX 01（同一個音訊檔時）；-1 表示到音訊檔結尾
    QString title;     ///< 沒有 TITLE 時是空字串
    QString performer; ///< 沒有時是空字串（呼叫端退回整張的 PERFORMER）
    QString songwriter;
};

/// 解析後的 .cue。
struct CueSheet {
    QString title;     ///< 專輯名（FILE 之前的 TITLE）
    QString performer; ///< 專輯藝人
    QString songwriter;
    /// FILE 之前的 REM（鍵大寫），例：GENRE、DATE、DISCNUMBER。
    QHash<QString, QString> rem;
    std::vector<QString> files; ///< FILE 的檔名，原樣（通常是相對於 .cue 的路徑）
    std::vector<CueTrack> tracks; ///< 依出現順序
};

/// 解析 .cue 文字（已解碼）。區段規則：每軌從自己的 INDEX 01 開始，到同一個音訊檔中下一軌的
/// INDEX 01 結束，所以下一軌的前置空白（INDEX 00 到 INDEX 01）算在上一軌的結尾，相鄰兩軌
/// 接起來剛好是連續的音訊（AC-07-2）；第一軌 INDEX 01 之前的部分（隱藏軌）不播。
/// 沒有任何 AUDIO 軌、時間格式錯誤、軌缺 INDEX 01、軌號重複時回傳 Error。
/// 執行緒：任何執行緒。
Result<CueSheet> parseCueSheet(const QString& text);

} // namespace auralis::core
