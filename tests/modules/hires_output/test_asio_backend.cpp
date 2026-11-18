// ASIO PCM 後端（US-03）：用假的 ASIO 驅動驗證裝置清單、位元完美、格式協商與被佔用的偵測。
#include "asio_backend.h"
#include "asio_backend_factory.h"
#include "fake_asio.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

using auralis::AudioFormat;
using auralis::IRenderSource;
using auralis::OutputErrorKind;
using auralis::mod::out::AsioBackend;
using auralis::mod::out::AsioBackendFactory;
using auralis::test::FakeAsio;
using auralis::test::FakeAsioDrivers;
using namespace std::chrono_literals;

namespace {

/// 依序交出 data 的樣本框，用完補 0；記下 render 被呼叫幾次。
class FakeSource final : public IRenderSource {
public:
    FakeSource(QByteArray data, int frameBytes)
        : m_data(std::move(data)), m_frameBytes(frameBytes) {}
    qint64 render(std::byte* dest, qint64 frames) noexcept override {
        calls.fetch_add(1);
        const qint64 want = frames * m_frameBytes;
        const qint64 have = std::min<qint64>(want, m_data.size() - m_offset);
        std::memcpy(dest, m_data.constData() + m_offset, static_cast<std::size_t>(have));
        std::memset(reinterpret_cast<char*>(dest) + have, 0, static_cast<std::size_t>(want - have));
        m_offset += have;
        return have / m_frameBytes;
    }
    std::atomic<int> calls{0};

private:
    QByteArray m_data;
    int m_frameBytes;
    qint64 m_offset = 0;
};

AudioFormat pcm(quint32 rate, quint16 bits, quint16 channels = 2) {
    AudioFormat format;
    format.rate = rate;
    format.bits = bits;
    format.container = bits == 16 ? 16 : 32;
    format.channels = channels;
    return format;
}

/// 隨機的已知訊號；24 位元左對齊在 32 位元容器時最低位元組是 0（和核心音源交出的一樣）。
QByteArray knownSignal(const AudioFormat& format, int frames, unsigned seed) {
    std::mt19937 random(seed);
    QByteArray data(frames * format.bytesPerFrame(), '\0');
    for (qsizetype i = 0; i < data.size(); ++i) {
        const bool padding = format.bits == 24 && i % 4 == 0;
        data[i] = padding ? '\0' : static_cast<char>(random() & 0xFF);
    }
    return data;
}

struct Rig {
    FakeAsio fake;
    std::shared_ptr<FakeAsioDrivers> drivers = std::make_shared<FakeAsioDrivers>();
    Rig() {
        drivers->driver = &fake;
        drivers->entries = {{QStringLiteral("{0B2C0F3E-1111-2222-3333-444455556666}"),
                             QStringLiteral("假的 ASIO")}};
    }
    std::unique_ptr<AsioBackend> backend(std::chrono::milliseconds stall = 1000ms) {
        return std::make_unique<AsioBackend>(drivers, drivers->entries.first(), 0, stall);
    }
};

} // namespace

TEST_CASE("AC-03-1：沒有安裝 ASIO 驅動時，ASIO 路徑沒有任何裝置，也不回報錯誤") {
    auto drivers = std::make_shared<FakeAsioDrivers>();
    AsioBackendFactory factory(drivers, 0);
    CHECK(factory.name() == QStringLiteral("ASIO"));
    CHECK(factory.devices().isEmpty());
}

TEST_CASE("AC-03-1：安裝了 ASIO 驅動時，裝置清單列出驅動名稱，id 是 CLSID") {
    Rig rig;
    AsioBackendFactory factory(rig.drivers, 0);
    const auto devices = factory.devices();
    REQUIRE(devices.size() == 1);
    CHECK(devices.first().name == QStringLiteral("假的 ASIO"));
    CHECK(devices.first().id == rig.drivers->entries.first().clsid);
    CHECK(factory.note() == QStringLiteral("位元完美"));
    CHECK(factory.create(devices.first().id).hasValue());
    CHECK_FALSE(factory.create(QStringLiteral("{不存在}")).hasValue());
}

TEST_CASE(
    "AC-03-2：ASIO 送進驅動緩衝的樣本與解碼後的 PCM 逐位元相同（16／24 位元，44.1–192 kHz）") {
    for (const quint32 rate : {44100u, 48000u, 96000u, 192000u}) {
        for (const quint16 bits : {quint16(16), quint16(24)}) {
            Rig rig;
            auto backend = rig.backend();
            const AudioFormat format = pcm(rate, bits);
            const auto opened = backend->open(format);
            REQUIRE(opened.hasValue());
            CHECK(opened.value().rate == rate);
            CHECK(opened.value().bits == bits);
            CHECK(rig.fake.rate == rate);
            const QByteArray signal = knownSignal(format, 64 * 6, rate + bits);
            FakeSource source(signal, format.bytesPerFrame());
            REQUIRE(backend->start(source).hasValue());
            for (int i = 0; i < 6; ++i) {
                rig.fake.tick();
            }
            backend->stop();
            const int sampleBytes = format.container / 8;
            for (int ch = 0; ch < 2; ++ch) {
                const auto& got = rig.fake.captured[ch];
                REQUIRE(got.size() == 64u * 6 * 4);
                bool same = true;
                for (int f = 0; f < 64 * 6; ++f) {
                    const char* in =
                        signal.constData() + f * format.bytesPerFrame() + ch * sampleBytes;
                    // Int32LSB：來源的位元原樣放在高位，16 位元來源低 16 位元補 0
                    for (int b = 0; b < 4; ++b) {
                        const int fromSource = b - (4 - sampleBytes);
                        const auto want = fromSource < 0
                                              ? std::uint8_t{0}
                                              : static_cast<std::uint8_t>(in[fromSource]);
                        same = same && got[f * 4 + b] == want;
                    }
                }
                CHECK(same);
            }
            backend->close();
        }
    }
}

TEST_CASE("AC-03-2：驅動是 Int24LSB 時 24 位元來源逐位元相同，16 位元驅動放不下時不降位元") {
    Rig rig;
    rig.fake.sampleType = ASIOSTInt24LSB;
    auto backend = rig.backend();
    const AudioFormat format = pcm(96000, 24);
    REQUIRE(backend->open(format).hasValue());
    const QByteArray signal = knownSignal(format, 64 * 2, 7);
    FakeSource source(signal, format.bytesPerFrame());
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    rig.fake.tick();
    backend->close();
    for (int f = 0; f < 128; ++f) {
        const char* in = signal.constData() + f * 8; // 左聲道：位元組 1–3 是 24 位元的值
        CHECK(std::memcmp(rig.fake.captured[0].data() + f * 3, in + 1, 3) == 0);
    }

    Rig narrow;
    narrow.fake.sampleType = ASIOSTInt16LSB;
    auto backend16 = narrow.backend();
    const auto opened = backend16->open(pcm(44100, 24));
    REQUIRE_FALSE(opened.hasValue());
    CHECK(backend16->lastErrorKind() == OutputErrorKind::FormatNotSupported);
    CHECK_FALSE(narrow.fake.buffersAlive);
}

TEST_CASE("AC-03-2：單聲道來源送到前兩個輸出聲道") {
    Rig rig;
    auto backend = rig.backend();
    const AudioFormat format = pcm(44100, 16, 1);
    REQUIRE(backend->open(format).hasValue());
    REQUIRE(rig.fake.channels == 2);
    FakeSource source(knownSignal(format, 64, 3), format.bytesPerFrame());
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    backend->close();
    CHECK(rig.fake.captured[0] == rig.fake.captured[1]);
}

TEST_CASE("AC-02-2（ASIO）：驅動不支援的取樣率回報格式不支援，不改用別的取樣率") {
    Rig rig;
    rig.fake.unsupportedRates = {192000};
    auto backend = rig.backend();
    const auto opened = backend->open(pcm(192000, 24));
    REQUIRE_FALSE(opened.hasValue());
    CHECK(backend->lastErrorKind() == OutputErrorKind::FormatNotSupported);
    CHECK(opened.error().message.contains(QStringLiteral("192000 Hz")));
    CHECK(rig.fake.setRateCalls == 0);
    CHECK(rig.fake.releases == 1); // 失敗時驅動有放掉，下一首可以再開
    CHECK(backend->open(pcm(96000, 24)).hasValue());
    backend->close();
}

TEST_CASE(
    "情況 1（AC-02-4，ASIO）：驅動被別的程式接手（要求重設）時回報裝置被佔用，關掉後可以重開") {
    Rig rig;
    auto backend = rig.backend();
    const AudioFormat format = pcm(44100, 16);
    REQUIRE(backend->open(format).hasValue());
    FakeSource source(knownSignal(format, 64 * 4, 9), format.bytesPerFrame());
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    CHECK_FALSE(backend->playbackError().has_value());

    CHECK(rig.fake.sendReset() == 1);
    const auto error = backend->playbackError();
    REQUIRE(error.has_value());
    CHECK(error->kind == OutputErrorKind::DeviceBusy);
    // 回報之後不再向來源要資料（驅動還在呼叫時送靜音）
    const int calls = source.calls.load();
    rig.fake.tick();
    CHECK(source.calls.load() == calls);
    CHECK(std::all_of(rig.fake.captured[0].end() - 64 * 4, rig.fake.captured[0].end(),
                      [](std::uint8_t b) { return b == 0; }));

    // 上層停止播放：錯誤在 stop、close 之後仍讀得到，重新開啟後清掉
    backend->stop();
    backend->close();
    CHECK(backend->playbackError().has_value());
    CHECK_FALSE(rig.fake.buffersAlive);
    REQUIRE(backend->open(format).hasValue());
    CHECK_FALSE(backend->playbackError().has_value());
    FakeSource again(knownSignal(format, 64, 10), format.bytesPerFrame());
    REQUIRE(backend->start(again).hasValue());
    rig.fake.tick();
    CHECK(again.calls.load() == 1);
    backend->close();
}

TEST_CASE("情況 1（AC-02-4，ASIO）：播放中回呼停止超過門檻時回報裝置被佔用，回呼持續時不誤報") {
    Rig rig;
    auto backend = rig.backend(100ms);
    const AudioFormat format = pcm(44100, 16);
    REQUIRE(backend->open(format).hasValue());
    FakeSource source(QByteArray(), format.bytesPerFrame());
    REQUIRE(backend->start(source).hasValue());
    rig.fake.tick();
    CHECK_FALSE(backend->playbackError().has_value());
    std::this_thread::sleep_for(150ms);
    rig.fake.tick(); // 回呼還在：不算被佔用
    CHECK_FALSE(backend->playbackError().has_value());
    std::this_thread::sleep_for(150ms); // 之後沒有回呼
    const auto error = backend->playbackError();
    REQUIRE(error.has_value());
    CHECK(error->kind == OutputErrorKind::DeviceBusy);
    backend->close();
}

TEST_CASE("AC-02-4（ASIO）：開始播放前驅動要求重設不算被接手") {
    Rig rig;
    auto backend = rig.backend();
    REQUIRE(backend->open(pcm(44100, 16)).hasValue());
    rig.fake.sendReset();
    CHECK_FALSE(backend->playbackError().has_value());
    backend->close();
}

TEST_CASE("AC-02-4（ASIO）：驅動 init 失敗時回報原因，不當掉") {
    Rig rig;
    rig.fake.initOk = false;
    auto backend = rig.backend();
    const auto opened = backend->open(pcm(44100, 16));
    REQUIRE_FALSE(opened.hasValue());
    CHECK(opened.error().message.contains(QStringLiteral("fake init failed")));
    CHECK(backend->lastErrorKind() == OutputErrorKind::Other);
    CHECK(rig.fake.releases == 1);
}

TEST_CASE("AC-03-2：同時只能開一個 ASIO 輸出；前一個關掉後可以開") {
    Rig first;
    Rig second;
    auto a = first.backend();
    auto b = second.backend();
    REQUIRE(a->open(pcm(44100, 16)).hasValue());
    CHECK_FALSE(b->open(pcm(44100, 16)).hasValue());
    a->close();
    CHECK(b->open(pcm(44100, 16)).hasValue());
    b->close();
}

TEST_CASE("AC-03-2：ASIO 驅動在同一條 STA 執行緒建立與控制，不論 open／close 從哪條執行緒呼叫") {
    Rig rig;
    auto backend = rig.backend();
    const AudioFormat format = pcm(48000, 24);
    FakeSource source(QByteArray(), format.bytesPerFrame());
    bool opened = false;
    bool started = false;
    std::thread decode([&] { // 相當於解碼執行緒（Catch2 的斷言不能在別的執行緒用）
        opened = backend->open(format).hasValue();
        started = opened && backend->start(source).hasValue();
    });
    decode.join();
    REQUIRE(opened);
    REQUIRE(started);
    backend->close(); // 測試執行緒（相當於 UI 執行緒）
    // 測試行程裡沒有別的 STA 時，第一個 STA 會被當成主 STA，兩者都是單執行緒套間
    CHECK((rig.fake.createApartment == APTTYPE_STA || rig.fake.createApartment == APTTYPE_MAINSTA));
    CHECK(rig.fake.controlThreads.size() == 1);
    CHECK_FALSE(rig.fake.controlThreads.count(QThread::currentThreadId()));
    CHECK(rig.fake.releases == 1);
}
