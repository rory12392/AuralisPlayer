#pragma once

#include <QString>

namespace auralis::test {

struct FakeModuleCounters {
    int okStarted = 0;
    int okStopped = 0;
    int failStarted = 0;
    int failStopped = 0;
    QString okHostCode;
    /// TESTOK 試寫別人的表的結果：denied（被白名單擋下）、error（沒有資料庫）、written。
    QString okDatabase;
};

/// 假模組的生命週期計數；每個測試開始前重設。
FakeModuleCounters& counters();

} // namespace auralis::test
