#pragma once

#include <auralis/result.h>

#include <QList>
#include <QString>
#include <QVariant>

#include <functional>

namespace auralis {

/// 模組寫自己擁有的資料表（每個模組一份，歸屬見資料模型的「模組擁有」表）。
/// 白名單外的表（核心的表、別的模組的表）不能讀也不能寫，建立或修改資料表也不行：
/// 這種程式錯誤丟 std::logic_error；可預期的失敗（約束衝突、資料庫被鎖住）回傳 Error。
/// 參數用 ? 依序綁定；QString 存成文字、整數與 bool 存成整數、QByteArray 存成 BLOB、
/// 空的 QVariant 存成 NULL。一次只能執行一句 SQL。
/// 執行緒：可以在任何執行緒呼叫（例：背景工作裡寫快取）；同一個模組的呼叫依序執行。
class ModuleDatabase {
public:
    using Row = QVariantList;

    virtual ~ModuleDatabase() = default;
    ModuleDatabase(const ModuleDatabase&) = delete;
    ModuleDatabase& operator=(const ModuleDatabase&) = delete;

    /// 執行一句寫入（INSERT、UPDATE、DELETE、REPLACE），回傳受影響的列數。
    virtual Result<qint64> execute(const QString& sql, const QVariantList& params = {}) = 0;

    /// 執行一句查詢，回傳所有列；每列的欄位順序和 SELECT 相同。
    virtual Result<QList<Row>> query(const QString& sql, const QVariantList& params = {}) = 0;

    /// 在一個交易裡執行 work：work 回傳 true 時提交，回傳 false 或丟出例外時還原
    /// （例外會繼續往外丟）。交易期間其他執行緒對同一個模組的呼叫會等它結束。
    /// 回傳 work 的結果；提交失敗時回傳 Error。
    virtual Result<bool> transaction(const std::function<bool()>& work) = 0;

protected:
    ModuleDatabase() = default;
};

} // namespace auralis
