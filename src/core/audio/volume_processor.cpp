#include "audio/volume_processor.h"

#include <QCoreApplication>

#include <algorithm>

namespace auralis::core {

double volumeGain(int percent) noexcept {
    const double p = std::clamp(percent, 0, 100) / 100.0;
    return p * p * p;
}

QString volumeReasonText(int percent) {
    return QCoreApplication::translate("auralis::core::VolumeProcessor", "音量 %1%").arg(percent);
}

void VolumeProcessor::setPercent(int percent) noexcept {
    m_percent.store(std::clamp(percent, 0, 100), std::memory_order_release);
}

void VolumeProcessor::latch() noexcept {
    m_target.store(volumeGain(m_percent.load(std::memory_order_acquire)),
                   std::memory_order_relaxed);
}

void VolumeProcessor::prepare(const AudioFormat& format) {
    m_rampFrames = std::max<qint64>(1, qint64{format.rate} * kRampMs / 1000);
    // 斜坡長度換了，下一個區塊照新的長度重算
    m_ramping = false;
}

bool VolumeProcessor::isActive() const noexcept {
    return m_target.load(std::memory_order_relaxed) != 1.0 ||
           m_current.load(std::memory_order_relaxed) != 1.0;
}

bool VolumeProcessor::isMuted() const noexcept {
    return m_target.load(std::memory_order_relaxed) == 0.0 &&
           m_current.load(std::memory_order_relaxed) == 0.0;
}

void VolumeProcessor::snap() noexcept {
    latch();
    m_current.store(m_target.load(std::memory_order_relaxed), std::memory_order_relaxed);
    m_ramping = false;
}

void VolumeProcessor::process(double* samples, qint64 frames, int channels) noexcept {
    const double target = m_target.load(std::memory_order_relaxed);
    double gain = m_current.load(std::memory_order_relaxed);
    if (gain != target && (!m_ramping || target != m_stepTarget)) {
        // 目標換了：從目前的增益重新走一段完整的斜坡
        m_stepTarget = target;
        m_step = (target - gain) / static_cast<double>(m_rampFrames);
        m_ramping = true;
    }
    qint64 frame = 0;
    for (; frame < frames && gain != target; ++frame) {
        gain += m_step;
        // 走到（或越過）目標就停在目標，之後是固定增益
        if ((m_step > 0.0 && gain >= target) || (m_step < 0.0 && gain <= target) || m_step == 0.0) {
            gain = target;
            m_ramping = false;
        }
        for (int ch = 0; ch < channels; ++ch) {
            samples[frame * channels + ch] *= gain;
        }
    }
    m_lastRampFrames = frame;
    for (qint64 i = frame * channels; i < frames * channels; ++i) {
        samples[i] *= gain;
    }
    m_current.store(gain, std::memory_order_relaxed);
}

} // namespace auralis::core
