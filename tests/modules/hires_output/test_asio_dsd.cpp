// ASIO 原生 DSD（AC-04-4）與原生 DSD 不能用時的提示和 DoP 退路（AC-04-2）：用假的 ASIO 驅動
// 驗證 kAsioSetIoFormat 成功與失敗、DSD 與 PCM 之間的切換、靜音碼（AC-04-6）。
#include "asio_backend.h"
#include "dop_test_helpers.h"
#include "dsd_fixtures.h"
#include "fake_asio.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QStringList>

#include <algorithm>
#include <cstring>

using auralis::AudioFormat;
using auralis::IRenderSource;
using auralis::mod::out::AsioBackend;
using auralis::test::FakeAsio;
using auralis::test::FakeAsioDrivers;
using namespace auralis::test;
using namespace std::chrono_literals;

namespace {

/// Spring 3 原生 DSD64 建議 32768 位元；測試用小一點的，回呼一次 16 個 DSD 樣本框。
constexpr long kDsdBufferBits = 512;
constexpr qint64 kDsdFramesPerTick = kDsdBufferBits / 32;

/// 依序交出 data 的樣本框，用完補 fill；記下 render 被呼叫幾次。
class BytesSource final : public IRenderSource {
public:
    BytesSource(QByteArray data, int frameBytes)
        : m_data(std::move(data)), m_frameBytes(frameBytes) {}
    qint64 render(std::byte* dest, qint64 frames) noexcept override {
        ++calls;
        const qint64 want = frames * m_frameBytes;
        const qint64 have = std::min<qint64>(want, m_data.size() - m_offset);
        std::memcpy(dest, m_data.constData() + m_offset, static_cast<std::size_t>(have));
        std::memset(reinterpret_cast<char*>(dest) + have, kDsdSilence,
                    static_cast<std::size_t>(want - have));
        m_offset += have;
        return have / m_frameBytes;
    }
    int calls = 0;

private:
    QByteArray m_data;
    int m_frameBytes;
    qint64 m_offset = 0;
};

struct Rig {
    FakeAsio fake;
    std::shared_ptr<FakeAsioDrivers> drivers = std::make_shared<FakeAsioDrivers>();
    QStringList notices;
    Rig() {
        drivers->driver = &fake;
        drivers->entries = {{QStringLiteral("{0B2C0F3E-1111-2222-3333-444455556666}"),
                             QStringLiteral("假的 ASIO")}};
        fake.preferredFrames = kDsdBufferBits;
    }
    std::unique_ptr<AsioBackend> backend() {
        return std::make_unique<AsioBackend>(drivers, drivers->entries.first(), 0, 1000ms,
                                             [this](const QString& text) { notices << text; });
    }
};

QByteArray bytesOf(const std::vector<std::uint8_t>& v) {
    return QByteArray(reinterpret_cast<const char*>(v.data()), static_cast<qsizetype>(v.size()));
}

/// ASIO 的兩個 Int32LSB 聲道緩衝合成交錯的 PCM（parseDop 吃的排列）。
QByteArray interleave32(const FakeAsio& fake) {
    QByteArray out;
    const std::size_t frames = fake.captured[0].size() / 4;
    for (std::size_t f = 0; f < frames; ++f) {
        for (long ch = 0; ch < fake.channels; ++ch) {
            out.append(reinterpret_cast<const char*>(fake.captured[ch].data() + f * 4), 4);
        }
    }
    return out;
}

/// 核心約定的 DSD 位元流（聲道以 4 位元組交錯）裡第 channel 個聲道依時間排列的位元組。
QByteArray channelBits(const QByteArray& dsd, int channels, int channel) {
    QByteArray out;
    for (qsizetype at = channel * 4; at < dsd.size(); at += channels * 4) {
        out.append(dsd.mid(at, 4));
    }
    return out;
}

} // namespace

TEST_CASE("AC-04-4：驅動接受 kAsioSetIoFormat 時以原生 DSD 播放，DSD 位元流逐位元送進驅動") {
    for (const quint32 rate : {2822400u, 5644800u, 11289600u}) {
        Rig rig;
        auto backend = rig.backend();
        const AudioFormat format = dsdAudio(rate);
        const auto opened = backend->open(format);
        REQUIRE(opened.hasValue());
        CHECK(opened.value() == format); // 輸出狀態的裝置格式是 DSD：原生
        CHECK(backend->dsdPath() == AsioBackend::DsdPath::Native);
        CHECK(rig.fake.createdDsd);
        CHECK(rig.fake.createdRate == rate); // 先切 DSD 再設位元率（PCM 狀態會回 ASE_NoClock）
        CHECK(rig.notices.isEmpty());
        const QByteArray signal = randomBytes(kDsdFramesPerTick * 8 * 5, rate / 1000);
        BytesSource source(signal, 8);
        REQUIRE(backend->start(source).hasValue());
        for (int i = 0; i < 5; ++i) {
            rig.fake.tick();
        }
        backend->stop();
        CHECK(bytesOf(rig.fake.captured[0]) == channelBits(signal, 2, 0));
        CHECK(bytesOf(rig.fake.captured[1]) == channelBits(signal, 2, 1));
        backend->close();
    }
}

TEST_CASE("AC-04-4：單聲道 DSD 以原生 DSD 送到前兩個輸出聲道") {
    Rig rig;
    auto backend = rig.backend();
    REQUIRE(backend->open(dsdAudio(2822400, 1)).hasValue());
    REQUIRE(rig.fake.channels == 2);
    const QByteArray signal = randomBytes(kDsdFramesPerTick * 4, 431);
    BytesSource source(signal, 4);
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    backend->close();
    CHECK(bytesOf(rig.fake.captured[0]) == signal);
    CHECK(rig.fake.captured[1] == rig.fake.captured[0]);
}

TEST_CASE("AC-04-2：kAsioSetIoFormat 失敗時提示「此驅動不支援原生 DSD，已改用 DoP」並在 ASIO "
          "上改送 DoP") {
    Rig rig;
    rig.fake.dsdSupported = false;
    auto backend = rig.backend();
    const auto opened = backend->open(dsdAudio(2822400));
    REQUIRE(opened.hasValue());
    // 輸出狀態的裝置格式是 PCM（DSD ÷ 16、24 位元）：DoP
    CHECK(opened.value() == pcmFormat(176400, 24));
    CHECK(backend->dsdPath() == AsioBackend::DsdPath::Dop);
    CHECK(rig.notices == QStringList{QStringLiteral("此驅動不支援原生 DSD，已改用 DoP")});
    CHECK_FALSE(rig.fake.createdDsd);
    CHECK(rig.fake.createdRate == 176400);
    const QByteArray signal = randomBytes(kDsdBufferBits / 2 * 8 * 3, 441);
    BytesSource source(signal, 8);
    REQUIRE(backend->start(source).hasValue());
    for (int i = 0; i < 3; ++i) {
        rig.fake.tick();
    }
    backend->close();
    const DopParse parsed = parseDop(interleave32(rig.fake), 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(parsed.firstMarker == 0x05);
    CHECK(parsed.dsd == signal); // DoP 不改位元流（逐位元還原得回來）
}

TEST_CASE("AC-04-2：切到 DSD 後聲道格式不是 DSDInt8MSB1 時也提示並退回 DoP，驅動切回 PCM") {
    Rig rig;
    rig.fake.dsdChannelType = false;
    auto backend = rig.backend();
    REQUIRE(backend->open(dsdAudio(5644800)).hasValue());
    CHECK(backend->dsdPath() == AsioBackend::DsdPath::Dop);
    CHECK(rig.notices.size() == 1);
    CHECK(rig.fake.ioFormats == std::vector<long>{kASIODSDFormat, kASIOPCMFormat});
    CHECK_FALSE(rig.fake.dsdIo);
    CHECK(rig.fake.createdRate == 352800);
    backend->close();
}

TEST_CASE("AC-04-4：原生 DSD 關閉時驅動切回 PCM，下一首 PCM 照常開；再播 DSD 又切回原生") {
    Rig rig;
    auto backend = rig.backend();
    REQUIRE(backend->open(dsdAudio(2822400)).hasValue());
    CHECK(rig.fake.dsdIo);
    backend->close();
    CHECK_FALSE(rig.fake.dsdIo);
    CHECK(rig.fake.ioFormats == std::vector<long>{kASIODSDFormat, kASIOPCMFormat});

    const auto pcm = backend->open(pcmFormat(44100, 16));
    REQUIRE(pcm.hasValue());
    CHECK(backend->dsdPath() == AsioBackend::DsdPath::None);
    CHECK(rig.fake.rate == 44100);
    CHECK_FALSE(rig.fake.createdDsd);
    backend->close();
    CHECK(rig.fake.ioFormats.size() == 2); // PCM 不碰 kAsioSetIoFormat

    REQUIRE(backend->open(dsdAudio(5644800)).hasValue());
    CHECK(backend->dsdPath() == AsioBackend::DsdPath::Native);
    CHECK(rig.fake.createdRate == 5644800);
    backend->close();
    CHECK_FALSE(rig.fake.dsdIo);
    CHECK(rig.notices.isEmpty());
}

TEST_CASE("AC-04-6：原生 DSD 不送曲目時（被接手後）送的是 DSD 靜音碼 0x69，不是 0") {
    Rig rig;
    auto backend = rig.backend();
    REQUIRE(backend->open(dsdAudio(2822400)).hasValue());
    BytesSource source(randomBytes(kDsdFramesPerTick * 8, 451), 8);
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    rig.fake.sendReset();
    REQUIRE(backend->playbackError().has_value());
    rig.fake.tick();
    backend->close();
    const QByteArray last = bytesOf(rig.fake.captured[0]).right(kDsdBufferBits / 8);
    CHECK(allDsdSilence(last));
    CHECK(source.calls == 1);
}

TEST_CASE("AC-04-6：DoP 退路不送曲目時送的是 DoP 靜音封包（0x69，標記照樣交替）") {
    Rig rig;
    rig.fake.dsdSupported = false;
    auto backend = rig.backend();
    REQUIRE(backend->open(dsdAudio(2822400)).hasValue());
    BytesSource source(randomBytes(kDsdBufferBits / 2 * 8, 461), 8);
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    rig.fake.sendReset();
    REQUIRE(backend->playbackError().has_value());
    rig.fake.tick();
    backend->close();
    const QByteArray all = interleave32(rig.fake);
    const DopParse parsed = parseDop(all, 2);
    INFO(parsed.problem.toStdString());
    REQUIRE(parsed.problem.isEmpty());
    CHECK(allDsdSilence(parsed.dsd.right(kDsdBufferBits / 2 * 8)));
}
