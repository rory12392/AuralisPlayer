#pragma once

#include <QString>
#include <QtGlobal>

namespace auralis {

/// 曲目身分：完整路徑加子曲目（0 表示整個檔案；CUE 曲目是軌號）。
/// 獨立成小標頭，讓處理器介面（audio_processor.h）不用拉進音樂庫查詢的依賴。
struct TrackRef {
    QString path;
    quint32 subsong = 0;
};

} // namespace auralis
