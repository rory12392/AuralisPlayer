// DSD 曲目的靜音（AC-04-6）：換格式排空與欠載時，引擎補的是包裝器轉換過的 DSD 靜音（0x69），
// 不是 0；PCM 路徑維持 0。用 Capture 後端與測試用的 DoP 包裝器逐框檢查。
#include "engine_helpers.h"
#include "signal_writer.h"

#include <auralis/dsd.h>

#include <catch2/catch_test_macros.hpp>

#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <deque>
#include <random>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::Result;
using auralis::core::CaptureBackend;
using auralis::core::EngineTrack;
using auralis::core::NextTrackProvider;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::test::fastCapture;
using auralis::test::MemorySource;

namespace {

/// 換格式時送的靜音下限（毫秒），和引擎的設定相同。
constexpr int kSilenceMs = 50;
constexpr quint8 kDsdSilence = 0x69;

/// 測試用的 DoP 包裝器（不記狀態）：每個 DSD 樣本框變成兩個 24-in-32 樣本框，
/// 標記依序是 0x05、0xFA，下面接 16 個 DSD 位元（先到的 8 位元在高位）。
class StatelessDopSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("DoP（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
        m_channels = format.channels;
        AudioFormat out;
        out.rate = format.dsdRate / 16;
        out.bits = 24;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames * 2; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        const auto* in = reinterpret_cast<const quint8*>(dsd);
        auto* samples = reinterpret_cast<quint32*>(out);
        for (qint64 f = 0; f < dsdFrames; ++f) {
            for (int half = 0; half < 2; ++half) {
                const quint32 mark = half == 0 ? 0x05u : 0xFAu;
                for (int ch = 0; ch < m_channels; ++ch) {
                    const quint8* bytes = in + (f * m_channels + ch) * 4 + half * 2;
                    *samples++ =
                        (mark << 24) | (quint32{bytes[0]} << 16) | (quint32{bytes[1]} << 8);
                }
            }
        }
        return dsdFrames * 2;
    }

private:
    int m_channels = 0;
};

AudioFormat dsd64() {
    AudioFormat format;
    format.dsdRate = 2822400;
    format.bits = 1;
    format.container = 32;
    format.channels = 2;
    return format;
}

AudioFormat cd() {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    return format;
}

/// 隨機的 DSD 位元流，刻意不含靜音碼 0x69，檢查時才分得出哪些是引擎補的靜音。
QByteArray dsdNoise(qint64 frames, unsigned seed) {
    QByteArray data(static_cast<qsizetype>(frames * 8), Qt::Uninitialized);
    std::mt19937 random(seed);
    for (char& byte : data) {
        quint8 value = static_cast<quint8>(random() & 0xFF);
        byte = static_cast<char>(value == kDsdSilence ? 0x96 : value);
    }
    return data;
}

/// 把 DoP 樣本框拆回每個聲道的 DSD 位元組；任何一框不是合法的 DoP 封包時 problem 不是空的。
struct DopParse {
    QString problem;
    QByteArray channel[2];
};

DopParse parseDop(const QByteArray& data, bool strictAlternation) {
    DopParse parsed;
    const qsizetype frames = data.size() / 8;
    quint32 previous = 0;
    for (qsizetype i = 0; i < frames && parsed.problem.isEmpty(); ++i) {
        quint32 marker = 0;
        for (int ch = 0; ch < 2; ++ch) {
            const auto sample = qFromLittleEndian<quint32>(data.constData() + (i * 2 + ch) * 4);
            const quint32 mark = sample >> 24;
            if ((mark != 0x05 && mark != 0xFA) || (sample & 0xFF) != 0 ||
                (ch == 1 && mark != marker)) {
                parsed.problem = QStringLiteral("第 %1 框不是 DoP 封包（0x%2）")
                                     .arg(i)
                                     .arg(sample, 8, 16, QLatin1Char('0'));
                break;
            }
            marker = mark;
            parsed.channel[ch].append(static_cast<char>((sample >> 16) & 0xFF));
            parsed.channel[ch].append(static_cast<char>((sample >> 8) & 0xFF));
        }
        if (parsed.problem.isEmpty() && strictAlternation && i > 0 && marker == previous) {
            parsed.problem = QStringLiteral("第 %1 框的標記沒有交替").arg(i);
        }
        previous = marker;
    }
    return parsed;
}

/// 輸入（以 4 位元組為單位交錯的兩個聲道）拆成各聲道的位元組。
QByteArray channelOf(const QByteArray& dsd, int ch) {
    QByteArray out;
    for (qsizetype at = ch * 4; at < dsd.size(); at += 8) {
        out.append(dsd.mid(at, 4));
    }
    return out;
}

bool allSilence(const QByteArray& bytes) {
    return std::all_of(bytes.begin(), bytes.end(),
                       [](char byte) { return static_cast<quint8>(byte) == kDsdSilence; });
}

QByteArray withoutSilence(QByteArray bytes) {
    bytes.removeIf([](char byte) { return static_cast<quint8>(byte) == kDsdSilence; });
    return bytes;
}

class OneNext final : public NextTrackProvider {
public:
    explicit OneNext(EngineTrack track) { m_track.push_back(std::move(track)); }
    std::optional<EngineTrack> nextTrack() override {
        if (m_track.empty()) {
            return std::nullopt;
        }
        EngineTrack track = std::move(m_track.front());
        m_track.pop_front();
        return track;
    }

private:
    std::deque<EngineTrack> m_track;
};

/// 開關打開後，下一次 read() 停住直到放行：讓緩衝讀空、造成欠載。
class GatedSource final : public auralis::IAudioSource {
public:
    GatedSource(AudioFormat format, QByteArray data) : m_inner(format, std::move(data)) {}
    AudioFormat format() const override { return m_inner.format(); }
    qint64 lengthFrames() const override { return m_inner.lengthFrames(); }
    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (armed.load() && !released.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return m_inner.read(dest, maxFrames);
    }
    Result<qint64> seek(qint64 frame) override { return m_inner.seek(frame); }

    std::atomic<bool> armed{false};
    std::atomic<bool> released{false};

private:
    MemorySource m_inner;
};

} // namespace

TEST_CASE("AC-04-6：從 DSD 換到 PCM，DoP 裝置關閉前送的都是 DoP 靜音封包（≥ 50 ms，不是 0）") {
    const QByteArray a = dsdNoise(8000, 471);
    const QByteArray b = auralis::test::makeNoise(cd(), 4410, 472);
    OneNext provider(EngineTrack{std::make_unique<MemorySource>(cd(), b), 0, -1, 2});
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<StatelessDopSink>());
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(EngineTrack{std::make_unique<MemorySource>(dsd64(), a), 0, -1, 1}, capture)
                .hasValue());
    REQUIRE(auralis::test::waitFinished(engine));
    CHECK_FALSE(engine.decodeError().has_value());
    const auto sessions = capture.sessions();
    REQUIRE(sessions.size() == 2);

    // DoP 段：曲目之後到關閉的每一框都是 DoP 封包、標記一路交替，內容全是靜音碼
    const QByteArray& dop = sessions[0].data;
    REQUIRE(dop.size() > a.size() * 2);
    const DopParse parsed = parseDop(dop, true);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    for (int ch = 0; ch < 2; ++ch) {
        const QByteArray input = channelOf(a, ch);
        CHECK(parsed.channel[ch].left(input.size()) == input);
        CHECK(allSilence(parsed.channel[ch].mid(input.size())));
    }
    const qsizetype tailFrames = (dop.size() - a.size() * 2) / 8;
    CHECK(tailFrames >= 176400LL * kSilenceMs / 1000);

    // PCM 段：開頭仍是數位靜音 0
    const qsizetype at = sessions[1].data.indexOf(b);
    REQUIRE(at >= 0);
    CHECK(at >= 44100LL * kSilenceMs / 1000 * 4);
    CHECK(std::all_of(sessions[1].data.begin(), sessions[1].data.begin() + at,
                      [](char byte) { return byte == 0; }));
}

TEST_CASE("AC-04-6：DSD 曲目欠載與播完時，補的是 DoP 靜音封包，不是 0") {
    const QByteArray a = dsdNoise(88200, 481); // DSD64 一秒
    auto gated = std::make_unique<GatedSource>(dsd64(), a);
    GatedSource* gate = gated.get();
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    engine.setDsdSink(std::make_shared<StatelessDopSink>());
    CaptureBackend capture(fastCapture());
    REQUIRE(engine.play(std::move(gated), capture).hasValue());
    gate->armed.store(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (engine.underruns() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(engine.underruns() > 0);
    gate->released.store(true);
    REQUIRE(auralis::test::waitFinished(engine));

    // 每一框都是 DoP 封包（欠載補的那段也是）；拿掉靜音碼之後就是原本的曲目
    const DopParse parsed = parseDop(capture.captured(), false);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    for (int ch = 0; ch < 2; ++ch) {
        const QByteArray input = channelOf(a, ch);
        CHECK(parsed.channel[ch].size() > input.size());
        CHECK(withoutSilence(parsed.channel[ch]) == input);
    }
}
