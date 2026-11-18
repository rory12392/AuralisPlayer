#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace auralis {

/// 可預期失敗的原因。message 給日誌與技術細節；使用者看到的文字由呼叫端用 qsTr 另外組。
struct Error {
    QString message;
    /// Windows API／COM 失敗時的 HRESULT；其他來源為 0。
    long hresult = 0;
    /// 失敗的呼叫名稱，例：IAudioClient::Initialize。
    QString call;
};

/// 可預期失敗的回傳值。MSVC 在 C++20 模式沒有 std::expected，所以用這個代替；
/// 介面刻意和 std::expected 相近，之後升級 C++23 時容易替換。
/// 執行緒：值型別，本身不做同步。
template <typename T> class Result {
public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool hasValue() const noexcept { return m_data.index() == 0; }
    explicit operator bool() const noexcept { return hasValue(); }

    /// 只能在 hasValue() 為 true 時呼叫。
    /// 右值版回傳值（從內部移出），暫存物件的 value() 不會懸空
    /// （例：range-for 走 f().value()；T-96）。
    T& value() & { return std::get<0>(m_data); }
    const T& value() const& { return std::get<0>(m_data); }
    T value() && { return std::get<0>(std::move(m_data)); }

    /// 只能在 hasValue() 為 false 時呼叫。
    const Error& error() const& { return std::get<1>(m_data); }

private:
    std::variant<T, Error> m_data;
};

/// 把失敗的 HRESULT 轉成 Error：message 是系統的說明文字（系統沒有時只有十六進位碼）。
/// 執行緒：任何非即時執行緒（會配置記憶體，即時音訊路徑不能用）。
Error hresultError(long hresult, const char* call);

} // namespace auralis

/// 檢查 Windows API／COM 回傳的 HRESULT：失敗時從所在函式 return 一個 Error
/// （含 HRESULT 與呼叫的原始碼文字），所以只能用在回傳 Result<T> 的函式裡。
/// 例：AURALIS_HR_CHECK(client->GetBufferSize(&frames));
#define AURALIS_HR_CHECK(expr)                                                                     \
    do {                                                                                           \
        const long auralisHr_ = static_cast<long>(expr);                                           \
        if (auralisHr_ < 0) {                                                                      \
            return ::auralis::hresultError(auralisHr_, #expr);                                     \
        }                                                                                          \
    } while (false)
