// OUT 模組與 output 類型外掛之間的介面（T-103）。只給 output 類型用，不是核心的公開介面。
// 官方外掛和主程式同一次建置（同一個編譯器、同一份 Qt 與 C++ 執行階段），所以 C++ 物件可以跨 DLL；
// 第三方外掛開放時要換成只用 C 型別的介面（下一版）。
#pragma once

#include <auralis/audio_output.h>

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

namespace auralis::mod::out {

/// 外掛進入點的 api 指標（AuralisPluginEntry::api）在 type 為 "output" 時指向這個介面。
/// 外掛 DLL 到程式結束都不卸載，物件由外掛擁有。
/// 執行緒：只在 UI 執行緒、模組 start() 時呼叫；建立的工廠與後端照 IOutputBackendFactory 的規則。
class IOutputPlugin {
public:
    virtual ~IOutputPlugin() = default;

    /// 建立輸出路徑的工廠，模組交給 OutputRegistry::add。
    /// window：主視窗 HWND（給驅動 init）；notice：原生 DSD 不能用而改送 DoP 時的提示
    /// （任何執行緒呼叫，模組自己轉回 UI 執行緒）。
    virtual std::shared_ptr<IOutputBackendFactory>
    createFactory(quintptr window, std::function<void(const QString&)> notice) = 0;

    /// 這個輸出路徑能不能收原生 DSD（有任何一個能收時，DSD 輸出方式的「原生 DSD」才可選）。
    virtual bool nativeDsd() const = 0;
};

} // namespace auralis::mod::out
