// ASIO 驅動的列舉與載入（只在 AURALIS_WITH_ASIO 時編譯）。測試換成假的驅動。
#pragma once

#include <auralis/result.h>

#include <QList>
#include <QString>

#include <memory>

struct IASIO;

namespace auralis::mod::out {

struct AsioDriverEntry {
    /// 驅動的 CLSID（{…}），當作輸出裝置的 id。
    QString clsid;
    /// 登錄機碼名稱，就是使用者看到的驅動名稱。
    QString name;
};

class AsioDrivers {
public:
    virtual ~AsioDrivers() = default;
    /// 列出安裝的驅動；沒有任何驅動時是空清單，不是錯誤（AC-03-1）。任何非即時執行緒。
    virtual QList<AsioDriverEntry> list() = 0;
    /// 建立驅動物件（還沒 init），回傳的介面由呼叫端 Release。
    /// 只能在已初始化 COM（STA）的執行緒呼叫：ASIO 驅動是單執行緒套間的 COM 物件。
    virtual Result<IASIO*> create(const QString& clsid) = 0;
};

/// 列舉 HKLM\SOFTWARE\ASIO、以 CoCreateInstance 載入的實作。
std::shared_ptr<AsioDrivers> systemAsioDrivers();

} // namespace auralis::mod::out
