#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>

namespace auralis::core {

/// 單一生產者（解碼執行緒）、單一消費者（即時音訊執行緒）的無鎖位元組環形緩衝。
/// 記憶體只在建構時配置；write()、read() 不配置、不加鎖、不丟例外，可以在即時路徑使用。
/// 讀寫位置是只增不減的 64 位元計數，容量是 2 的冪次，用遮罩取位置。
class RingBuffer {
public:
    /// 容量至少 minBytes，向上取到 2 的冪次。
    explicit RingBuffer(std::size_t minBytes) : m_capacity(roundUp(minBytes)) {
        m_data = std::make_unique<std::byte[]>(m_capacity);
    }
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    std::size_t capacity() const noexcept { return m_capacity; }

    /// 可以讀的位元組數（消費者呼叫時是下限，生產者呼叫時是上限）。
    std::size_t readable() const noexcept {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire);
    }
    /// 可以寫的位元組數。
    std::size_t writable() const noexcept { return m_capacity - readable(); }

    /// 生產者：最多寫 bytes 個位元組，回傳實際寫入數。
    std::size_t write(const std::byte* src, std::size_t bytes) noexcept {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        const std::size_t n = bytes < m_capacity - (w - r) ? bytes : m_capacity - (w - r);
        copyIn(w, src, n);
        m_write.store(w + n, std::memory_order_release);
        return n;
    }

    /// 消費者：最多讀 bytes 個位元組，回傳實際讀出數。
    std::size_t read(std::byte* dest, std::size_t bytes) noexcept {
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t n = bytes < w - r ? bytes : w - r;
        copyOut(r, dest, n);
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    /// 消費者：丟掉最多 bytes 個位元組不讀（跳轉時清掉舊位置的資料），回傳實際丟掉數。
    std::size_t discard(std::size_t bytes) noexcept {
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t n = bytes < w - r ? bytes : w - r;
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    /// 消費者：把還沒讀的資料從尾端（最晚寫入的那一頭）收回最多 bytes 個位元組，回傳實際收回數。
    /// 寫入位置平常只有生產者能動，所以只能在生產者停下來等、不呼叫 write() 的期間用
    /// （音量改變時收回還沒送出的資料，由解碼執行緒用新音量重做）。
    std::size_t unwrite(std::size_t bytes) noexcept {
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t n = bytes < w - r ? bytes : w - r;
        m_write.store(w - n, std::memory_order_release);
        return n;
    }

private:
    static std::size_t roundUp(std::size_t n) noexcept {
        std::size_t c = 1;
        while (c < n) {
            c <<= 1;
        }
        return c;
    }

    void copyIn(std::size_t pos, const std::byte* src, std::size_t n) noexcept {
        const std::size_t at = pos & (m_capacity - 1);
        const std::size_t first = n < m_capacity - at ? n : m_capacity - at;
        std::memcpy(m_data.get() + at, src, first);
        std::memcpy(m_data.get(), src + first, n - first);
    }

    void copyOut(std::size_t pos, std::byte* dest, std::size_t n) const noexcept {
        const std::size_t at = pos & (m_capacity - 1);
        const std::size_t first = n < m_capacity - at ? n : m_capacity - at;
        std::memcpy(dest, m_data.get() + at, first);
        std::memcpy(dest + first, m_data.get(), n - first);
    }

    static_assert(std::atomic<std::size_t>::is_always_lock_free, "即時路徑需要無鎖的原子計數");

    const std::size_t m_capacity;
    std::unique_ptr<std::byte[]> m_data;
    std::atomic<std::size_t> m_write{0};
    std::atomic<std::size_t> m_read{0};
};

} // namespace auralis::core
