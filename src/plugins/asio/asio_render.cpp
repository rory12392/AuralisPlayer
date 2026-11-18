// AsioBackend 回呼的資料路徑：向來源要資料、依格式（PCM、原生 DSD、DoP 退路）搬進驅動緩衝，
// 以及各格式的靜音（AC-04-6）。
#include "asio_backend.h"
#include "asio_dsd.h"
#include "asio_samples.h"

#include <windows.h>

#include <objbase.h>

#include <asiosys.h>

#include <asio.h>
#include <iasiodrv.h>

#include <algorithm>
#include <cstring>

namespace auralis::mod::out {

namespace {

/// DSD 的靜音碼：每個位元組 0x69（交替的 0 與 1，平均值為 0）。
constexpr std::byte kDsdSilence{0x69};

} // namespace

void AsioBackend::onBufferSwitch(long index) noexcept {
    // 即時路徑：不配置、不加鎖、不 I/O、不呼叫 Qt
    m_switches.fetch_add(1, std::memory_order_relaxed);
    index &= 1;
    IRenderSource* source = m_source.load(std::memory_order_acquire);
    if (source == nullptr || m_failure.load(std::memory_order_acquire) != 0) {
        fillSilence(index);
    } else {
        source->render(m_scratch.data(), m_renderFrames);
        void* const* outputs = m_halves[index].data();
        const int outputCount = static_cast<int>(m_halves[index].size());
        switch (m_dsdPath) {
        case DsdPath::Native:
            deinterleaveDsdToAsio(m_scratch.data(), m_requested.channels, m_renderFrames, outputs,
                                  outputCount);
            break;
        case DsdPath::Dop:
            m_dop.convert(m_scratch.data(), m_renderFrames, m_dopScratch.data());
            deinterleaveToAsio(m_dopScratch.data(), m_requested, m_bufferFrames, outputs,
                               outputCount, m_deviceBytes);
            break;
        case DsdPath::None:
            deinterleaveToAsio(m_scratch.data(), m_requested, m_bufferFrames, outputs, outputCount,
                               m_deviceBytes);
            break;
        }
    }
    if (m_postOutput) {
        m_driver->outputReady();
    }
}

void AsioBackend::fillSilence(long index) noexcept {
    for (void* buffer : m_halves[index & 1]) {
        std::memcpy(buffer, m_silenceHalf.data(), m_halfBytes);
    }
}

void AsioBackend::prepareSilence() {
    m_silenceHalf.assign(m_halfBytes, std::byte{0});
    if (m_dsdPath == DsdPath::Native) {
        std::fill(m_silenceHalf.begin(), m_silenceHalf.end(), kDsdSilence);
    } else if (m_dsdPath == DsdPath::Dop) {
        // 靜音碼經 DoP 包裝成封包（標記照樣交替），再照驅動的樣本格式放好：一個聲道就夠，
        // 每個聲道的靜音都一樣
        std::fill(m_scratch.begin(), m_scratch.end(), kDsdSilence);
        m_dop.convert(m_scratch.data(), m_renderFrames, m_dopScratch.data());
        void* out = m_silenceHalf.data();
        deinterleaveToAsio(m_dopScratch.data(), m_requested, m_bufferFrames, &out, 1,
                           m_deviceBytes);
    }
}

} // namespace auralis::mod::out
