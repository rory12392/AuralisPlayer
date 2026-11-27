#pragma once

#include <QtGlobal>

#include <functional>

namespace auralis::core {

class SqliteConnection;

struct BackfillCallbacks {
    /// 回傳 true 時盡快結束：已算好的那幾首照常寫入，其餘留給下次。
    std::function<bool()> cancelled = [] {
        return false;
    };
    /// 已處理的首數與開始時待補的總數。
    std::function<void(int done, int total)> progress = [](int, int) {
    };
};

struct BackfillResult {
    int total = 0;    ///< 開始時待補的首數
    int updated = 0;  ///< 這次補完的首數（之後不再是待補）
    int deferred = 0; ///< 檔案不在（磁碟離線、被移走）而留到下次啟動的首數
    bool cancelled = false;
};

/// 資料庫升級（0002）後的背景補值：把 meta_backfill = 1 的曲目補上 bitrate 與 added_at，
/// 補完的改成 0（之後 NULL 代表確實未知）。依 tracks.id 由小到大分批處理，每批一個短交易，
/// 中斷（取消、程式結束、當掉）後再呼叫一次就從還沒補的接著做，補過的不重做（AC-10-5）。
/// - bitrate：規則同掃描（audioFileBitrateKbps）；無損的整檔曲目直接用資料庫裡的檔案大小與長度，
///   不讀檔；有損格式讀 FFmpeg 的值；CUE 曲目讀 .cue，用 FILE 指向的整軌檔。
/// - added_at：檔案的建立時間（UTC 毫秒）；CUE 曲目是 .cue 檔的建立時間。
/// 檔案只讀屬性與檔頭，不修改。檔案不存在時那一首維持待補（這次跳過，下次啟動再試）。
/// 資料庫錯誤丟 std::runtime_error。執行緒：工作執行緒；db 是這個工作專用的連線。
BackfillResult runMetaBackfill(SqliteConnection& db, const BackfillCallbacks& callbacks = {},
                               int batchSize = 256);

/// 還有幾首待補。資料庫錯誤丟 std::runtime_error。執行緒：同 runMetaBackfill。
int pendingBackfillCount(SqliteConnection& db);

} // namespace auralis::core
