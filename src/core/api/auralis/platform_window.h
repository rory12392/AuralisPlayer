#pragma once

#include <QtGlobal>

namespace auralis {

/// 主視窗的系統資訊（SMTC 要 HWND）。主視窗在模組 start() 之前就建立好。
class PlatformWindow {
public:
    virtual ~PlatformWindow() = default;
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;

    /// 主視窗的 HWND（轉成整數，避免公開介面引入 windows.h）；沒有主視窗時（例：自我檢查）為 0。
    virtual quintptr hwnd() const = 0;

protected:
    PlatformWindow() = default;
};

} // namespace auralis
