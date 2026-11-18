#include "soak.h"

#include "audio/capture_backend.h"
#include "audio/next_track.h"
#include "audio/playback_engine.h"
#include "audio/processor_chain_impl.h"
#include "ui/ui_registry_impl.h"

#include <auralis/audio_source.h>
#include <auralis/dsd.h>

#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

namespace auralis::app {

namespace {

/// 每首測試訊號的長度：短到幾秒內就涵蓋換格式與同格式接續，又比預開下一首的時間長。
constexpr qint64 kTrackSeconds = 2;
/// 測試訊號的振幅（滿刻度的比例）與 PCM 的頻率：2 秒剛好是整數個週期，曲目頭尾都在零點，
/// 同格式接續與換格式前後都是連續的，不連續檢查才分得出真正的缺口。
constexpr double kAmplitude = 0.5;
constexpr int kPcmToneHz = 1000;
/// 不連續的門檻（滿刻度的比例）：1 kHz、振幅 0.5 的正弦在 44.1 kHz 相鄰樣本最多差約 0.07；
/// 中間掉一段資料（補 0）在波峰附近會差到 0.5。
constexpr double kStepThreshold = 0.15;
/// DSD64 測試音一個週期的樣本框數：90 框（2880 個位元）＝ 980 Hz，2 秒剛好 1960 個週期。
constexpr qint64 kDsdPeriodFrames = 90;
constexpr quint32 kDsd64Rate = 2822400;

bool isDsd(const AudioFormat& format) {
    return format.dsdRate > 0;
}

/// 第 token 首的格式。只有 PCM 時：44.1/16 一首，之後兩首一組交替（192/24、192/24、44.1/16…），
/// 每兩首換一次格式、中間夾一次同格式接續；192/24 是最重的 PCM 負載（AC-17-5 也用它）。
/// 有 DSD 時三種輪流（DSD64 一首，之後 44.1/16、192/24、DSD64 各兩首）：第一次換格式就是
/// DSD → PCM（轉 PCM 的包裝器要把曲目最後一段送完的地方），長一點會涵蓋 PCM → DSD。
AudioFormat formatOf(quint64 token, bool withDsd) {
    AudioFormat format;
    format.channels = 2;
    const quint64 pair = (token + 1) / 2;
    const quint64 kind = withDsd ? pair % 3 : (pair % 2 == 0 ? 1 : 2);
    if (kind == 0) {
        format.dsdRate = kDsd64Rate;
        format.bits = 1;
        format.container = 32;
    } else if (kind == 1) {
        format.rate = 44100;
        format.bits = 16;
        format.container = 16;
    } else {
        format.rate = 192000;
        format.bits = 24;
        format.container = 32;
    }
    return format;
}

/// 在記憶體裡產生正弦的 PCM 音源（兩個聲道相同），不經檔案與解碼器：
/// soak 量的是引擎與裝置節奏，不碰使用者的檔案。
class SignalSource final : public IAudioSource {
public:
    explicit SignalSource(AudioFormat format) : m_format(format) {}

    AudioFormat format() const override { return m_format; }
    qint64 lengthFrames() const override { return m_format.rate * kTrackSeconds; }
    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const qint64 frames = std::min(maxFrames, lengthFrames() - m_pos);
        const double step = 2.0 * std::numbers::pi * kPcmToneHz / m_format.rate;
        for (qint64 f = 0; f < frames; ++f) {
            const double value = kAmplitude * std::sin(step * static_cast<double>(m_pos + f));
            for (int ch = 0; ch < m_format.channels; ++ch) {
                const qint64 i = f * m_format.channels + ch;
                if (m_format.container == 16) {
                    const auto sample = static_cast<qint16>(std::lround(value * 32767.0));
                    std::memcpy(dest + i * 2, &sample, sizeof(sample));
                } else {
                    // 24 位元左對齊在 32 位元容器裡，低 8 位元為 0
                    const auto sample = static_cast<qint32>(std::lround(value * 8388607.0)) * 256;
                    std::memcpy(dest + i * 4, &sample, sizeof(sample));
                }
            }
        }
        m_pos += frames;
        return frames;
    }
    Result<qint64> seek(qint64 frame) override {
        m_pos = frame;
        return frame;
    }

private:
    AudioFormat m_format;
    qint64 m_pos = 0;
};

/// 一個週期的 DSD64 測試音（每聲道 4 個位元組一框、MSB first）：二階三角積分調變，
/// 先跑幾個週期讓積分器穩定再取一個週期循環使用（接縫的誤差在 20 kHz 以上，濾波後看不到）。
std::vector<std::byte> dsdTonePeriod() {
    constexpr qint64 bits = kDsdPeriodFrames * 32;
    constexpr int warmup = 20;
    std::vector<std::byte> period(static_cast<std::size_t>(kDsdPeriodFrames * 4), std::byte{0});
    double i1 = 0.0;
    double i2 = 0.0;
    double y = 0.0;
    for (qint64 n = 0; n < bits * (warmup + 1); ++n) {
        const double x =
            kAmplitude * std::sin(2.0 * std::numbers::pi * static_cast<double>(n % bits) / bits);
        i1 += x - y;
        i2 += i1 - y;
        const bool one = i2 >= 0.0;
        y = one ? 1.0 : -1.0;
        const qint64 at = n - bits * warmup;
        if (at >= 0 && one) {
            period[static_cast<std::size_t>(at / 8)] |=
                std::byte{static_cast<quint8>(0x80 >> (at % 8))};
        }
    }
    return period;
}

/// 在記憶體裡產生 DSD64 測試音的音源（兩個聲道相同，排列照 DsdFormat 的約定）。
class DsdSignalSource final : public IAudioSource {
public:
    DsdSignalSource(AudioFormat format, std::shared_ptr<const std::vector<std::byte>> period)
        : m_format(format), m_period(std::move(period)) {}

    AudioFormat format() const override { return m_format; }
    qint64 lengthFrames() const override { return m_format.dsdRate / 32 * kTrackSeconds; }
    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const qint64 frames = std::min(maxFrames, lengthFrames() - m_pos);
        for (qint64 f = 0; f < frames; ++f) {
            const std::byte* bits = m_period->data() + (m_pos + f) % kDsdPeriodFrames * 4;
            for (int ch = 0; ch < m_format.channels; ++ch) {
                std::memcpy(dest + (f * m_format.channels + ch) * 4, bits, 4);
            }
        }
        m_pos += frames;
        return frames;
    }
    Result<qint64> seek(qint64 frame) override {
        m_pos = frame;
        return frame;
    }

private:
    AudioFormat m_format;
    std::shared_ptr<const std::vector<std::byte>> m_period;
    qint64 m_pos = 0;
};

/// 無限的下一首：第 token 首之後永遠是第 token + 1 首。
class SignalProvider final : public core::NextTrackProvider {
public:
    explicit SignalProvider(bool withDsd) : m_withDsd(withDsd) {
        if (withDsd) {
            m_dsdPeriod = std::make_shared<const std::vector<std::byte>>(dsdTonePeriod());
        }
    }

    core::EngineTrack track(quint64 token) const {
        const AudioFormat format = formatOf(token, m_withDsd);
        core::EngineTrack next;
        if (isDsd(format)) {
            next.source = std::make_unique<DsdSignalSource>(format, m_dsdPeriod);
        } else {
            next.source = std::make_unique<SignalSource>(format);
        }
        next.token = token;
        return next;
    }
    std::optional<core::EngineTrack> nextTrack() override { return track(++m_last); }
    std::optional<core::EngineTrack> nextTrackAfter(quint64 token) override {
        m_last = token;
        return nextTrack();
    }

private:
    bool m_withDsd;
    std::shared_ptr<const std::vector<std::byte>> m_dsdPeriod;
    quint64 m_last = 0;
};

} // namespace

SoakResult runSoak(int seconds, bool withDsd, std::shared_ptr<DsdSink> dsdSink,
                   const std::atomic_bool* stop) {
    using Clock = std::chrono::steady_clock;
    SoakResult result;
    result.dsdRequested = withDsd;
    const bool dsd = withDsd && dsdSink != nullptr;
    core::ProcessorChainImpl chain;
    core::PlaybackEngine engine(chain);
    if (dsd) {
        result.dsdSink = dsdSink->name();
        engine.setDsdSink(std::move(dsdSink));
    } else if (withDsd) {
        result.dsdSkipped =
            QStringLiteral("沒有註冊 DSD 包裝器（進階輸出模組停用，或沒有選 DSD 輸出方式）");
    }
    // 實際時間的裝置節奏（每 10 毫秒一個裝置緩衝）；不記錄資料，只計數與檢查不連續
    core::CaptureBackend::Options options;
    options.maxCaptureSeconds = 0;
    options.continuityThreshold = kStepThreshold;
    core::CaptureBackend capture(options);
    SignalProvider provider(dsd);
    engine.setNextTrackProvider(&provider);
    result.stepThreshold = kStepThreshold;

    const auto start = Clock::now();
    auto status = engine.play(provider.track(0), capture);
    if (!status) {
        result.error = status.error().message;
        return result;
    }
    result.tracks = 1;
    result.dsdTracks = isDsd(formatOf(0, dsd)) ? 1 : 0;
    quint64 current = 0;
    const auto deadline = start + std::chrono::seconds(seconds);
    while (Clock::now() < deadline && (stop == nullptr || !stop->load())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (const auto failed = engine.outputError()) {
            result.error = failed->error.message;
            break;
        }
        if (engine.isFinished()) {
            const auto decodeError = engine.decodeError();
            result.error = decodeError ? decodeError->message : QStringLiteral("播放提前結束");
            break;
        }
        const quint64 token = engine.position().token;
        if (token != current) {
            ++result.tracks;
            const AudioFormat format = formatOf(token, dsd);
            ++(format == formatOf(current, dsd) ? result.sameFormatJoins : result.formatChanges);
            result.dsdTracks += isDsd(format) ? 1 : 0;
            current = token;
        }
    }
    result.underruns = engine.underruns();
    engine.stop();
    // 後端已停，音訊執行緒不再更新檢查結果
    result.pcmJumps = capture.continuity().pcmJumps();
    result.dopBreaks = capture.continuity().dopBreaks();
    result.maxPcmStep = capture.continuity().maxPcmStep();
    result.seconds = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count());
    return result;
}

QStringList soakErrors(const SoakResult& result) {
    QStringList errors;
    if (!result.error.isEmpty()) {
        errors << QStringLiteral("長時間播放中途失敗：%1").arg(result.error);
    }
    if (result.underruns > 0) {
        errors << QStringLiteral("長時間播放欠載 %1 次（Capture 後端，%2 秒）")
                      .arg(result.underruns)
                      .arg(result.seconds);
    }
    if (result.pcmJumps > 0) {
        errors << QStringLiteral("長時間播放 PCM 不連續 %1 處（相鄰樣本差超過 %2 滿刻度，最大 %3）")
                      .arg(result.pcmJumps)
                      .arg(result.stepThreshold)
                      .arg(result.maxPcmStep, 0, 'f', 3);
    }
    if (result.dopBreaks > 0) {
        errors << QStringLiteral("長時間播放 DoP 標記沒有交替 %1 框").arg(result.dopBreaks);
    }
    if (result.formatChanges == 0) {
        errors << QStringLiteral("長時間播放沒有涵蓋換格式（秒數至少 %1）").arg(kMinSoakSeconds);
    }
    if (result.sameFormatJoins == 0) {
        errors
            << QStringLiteral("長時間播放沒有涵蓋同格式接續（秒數至少 %1）").arg(kMinSoakSeconds);
    }
    return errors;
}

QJsonObject soakJson(const SoakResult& result) {
    QJsonObject json{{QStringLiteral("backend"), QStringLiteral("Capture")},
                     {QStringLiteral("seconds"), result.seconds},
                     {QStringLiteral("tracks"), result.tracks},
                     {QStringLiteral("formatChanges"), result.formatChanges},
                     {QStringLiteral("sameFormatJoins"), result.sameFormatJoins},
                     {QStringLiteral("underruns"), result.underruns},
                     {QStringLiteral("pcmJumps"), result.pcmJumps},
                     {QStringLiteral("dopBreaks"), result.dopBreaks},
                     {QStringLiteral("maxPcmStep"), result.maxPcmStep},
                     {QStringLiteral("stepThreshold"), result.stepThreshold}};
    if (result.dsdRequested) {
        json.insert(QStringLiteral("dsd"),
                    result.dsdSkipped.isEmpty()
                        ? QJsonObject{{QStringLiteral("sink"), result.dsdSink},
                                      {QStringLiteral("tracks"), result.dsdTracks}}
                        : QJsonObject{{QStringLiteral("skipped"), result.dsdSkipped}});
    }
    return json;
}

QString openSoakFullscreen(core::NavigatorImpl& navigator) {
    if (navigator.enterFullscreen()) {
        return {};
    }
    return QStringLiteral(
        "--fullscreen：沒有登記全螢幕頁（歌詞模組停用？），無法在全螢幕下做長時間播放");
}

void watchSoakFullscreen(core::NavigatorImpl& navigator, FullscreenSoakWatch& watch) {
    watch.connection = QObject::connect(
        &navigator, &core::NavigatorImpl::currentChanged, &navigator, [&navigator, &watch] {
            if (navigator.fullscreen() || watch.leftFullscreen) {
                return;
            }
            watch.leftFullscreen = true;
            watch.interruptions << QStringLiteral(
                "--fullscreen：長時間播放跑完前離開全螢幕頁，不是全程在全螢幕下播放");
        });
}

void finishSoakAfterUi(QThread& soakThread, FullscreenSoakWatch& watch) {
    QObject::disconnect(watch.connection);
    // soak 跑完時才會結束事件迴圈；執行緒還在跑表示是主視窗先結束（關掉視窗）
    if (!soakThread.isFinished()) {
        watch.interruptions << QStringLiteral(
            "--fullscreen：主視窗在長時間播放跑完前結束（關掉了視窗？），長時間播放提前停止");
        watch.stop = true;
    }
    soakThread.wait();
}

} // namespace auralis::app
