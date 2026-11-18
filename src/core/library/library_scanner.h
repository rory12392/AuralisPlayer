#pragma once

#include "library/library_writer.h"

#include <QList>
#include <QString>

#include <functional>

namespace auralis::core {

class SqliteConnection;

/// 一個音樂資料夾（library_roots 的一列）。
struct LibraryRoot {
    qint64 id = 0;
    QString path;
};

struct ScanCallbacks {
    /// 回傳 true 時盡快結束；已寫入的部分保留，報告標記為取消。
    std::function<bool()> cancelled = [] {
        return false;
    };
    /// 已檢查的音樂檔數與總數（總數在列舉完所有資料夾後才知道，之前是 0）。
    std::function<void(int scanned, int total)> progress = [](int, int) {
    };
};

/// 掃描音樂資料夾並把結果寫進資料庫：列舉（不開檔）→ 與資料庫比對大小與修改時間 →
/// 新的、改過的、上次讀不到的檔案平行讀標籤 → 分批寫入 → 移除已不存在的檔案與資料夾。
/// 讀不到的檔案與資料夾記在報告裡，不中斷掃描（AC-08-5）；讀不到的資料夾底下的資料不會被當成刪除。
/// 檔案一律唯讀開啟（AC-08-4）。資料庫錯誤丟 std::runtime_error。
/// 執行緒：工作執行緒（JobRunner 的工作）；db 是這次掃描專用的連線。
ScanReport scanLibrary(SqliteConnection& db, const QList<LibraryRoot>& roots,
                       const ScanCallbacks& callbacks);

/// 讀標籤的平行執行緒數（硬碟上太多執行緒反而變慢，所以有上限）。
int tagReaderThreads();

} // namespace auralis::core
