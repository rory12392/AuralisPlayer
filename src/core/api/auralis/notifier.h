#pragma once

#include <QString>

#include <functional>

namespace auralis {

/// 通知列（播放列上方）。一般通知 6 秒後自動收起，錯誤要使用者關掉；新的通知取代舊的。
/// 文字照介面規範的句型（發生什麼＋能做什麼），呼叫端用 tr() 翻好。只在 UI 執行緒呼叫。
class Notifier {
public:
    enum class Kind { Info, Error };

    virtual ~Notifier() = default;
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    /// actionText 不是空的時候通知列帶一個按鈕（例：「復原」「重試」），按下呼叫 action。
    virtual void notify(const QString& text, Kind kind = Kind::Info, const QString& actionText = {},
                        std::function<void()> action = {}) = 0;

protected:
    Notifier() = default;
};

} // namespace auralis
