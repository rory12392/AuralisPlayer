// 即時音訊路徑規則（Architecture §8）：音訊執行緒登記 MMCSS，而且播放中不配置記憶體。
// 這個檔案替換整個測試程式的 operator new，計算在被監看的執行緒上配置了幾次。
#include "engine_helpers.h"
#include "fake_wasapi.h"
#include "signal_writer.h"

#include "audio/wasapi_backend.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<unsigned long> g_watchedThread{0};
std::atomic<int> g_watchedAllocations{0};

void* allocate(std::size_t size) {
    const unsigned long watched = g_watchedThread.load(std::memory_order_relaxed);
    if (watched != 0 && watched == GetCurrentThreadId()) {
        g_watchedAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}
void* operator new[](std::size_t size) {
    return allocate(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

using auralis::AudioFormat;
using auralis::IProcessor;
using auralis::IProcessorFactory;
using auralis::core::CaptureBackend;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using auralis::core::WasapiBackend;
using auralis::core::WasapiMode;
using auralis::test::FakeDevice;
using auralis::test::FakeEndpoints;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::supportsOnly;

namespace {

class HalfGain final : public IProcessor {
public:
    void prepare(const AudioFormat&) override {}
    bool isActive() const noexcept override { return true; }
    QString reason() const override { return QStringLiteral("半增益"); }
    void process(double* samples, qint64 frames, int channels) noexcept override {
        for (qint64 i = 0; i < frames * channels; ++i) {
            samples[i] *= 0.5;
        }
    }
};

class HalfGainFactory final : public IProcessorFactory {
public:
    QString name() const override { return QStringLiteral("half"); }
    std::unique_ptr<IProcessor> create() override { return std::make_unique<HalfGain>(); }
};

} // namespace

TEST_CASE("即時路徑：音訊執行緒登記 MMCSS，播放中（含欠載與結尾）不配置記憶體") {
    const bool withProcessor = GENERATE(false, true);
    CAPTURE(withProcessor);
    AudioFormat format;
    format.rate = 96000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    ProcessorChainImpl chain;
    if (withProcessor) {
        chain.add(std::make_shared<HalfGainFactory>());
    }
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    auto source = std::make_unique<MemorySource>(format, makeNoise(format, 48000, 5));
    REQUIRE(engine.play(std::move(source), capture).hasValue());
    while (capture.audioThreadId() == 0) {
        std::this_thread::yield();
    }
    g_watchedAllocations = 0;
    g_watchedThread = capture.audioThreadId();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(engine.isFinished());
    // 播完之後讓裝置再跑一段，連「緩衝已空、補靜音」的路徑也算進去
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    g_watchedThread = 0;
    engine.stop();
    CHECK(capture.mmcssRegistered());
    CHECK(g_watchedAllocations.load() == 0);
}

TEST_CASE("即時路徑：播放中改音量（收回緩衝重做）的期間，音訊執行緒不配置記憶體") {
    AudioFormat format;
    format.rate = 96000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    CaptureBackend capture(fastCapture());
    auto source = std::make_unique<MemorySource>(format, makeNoise(format, 96000 * 2, 8));
    REQUIRE(engine.play(std::move(source), capture).hasValue());
    while (capture.audioThreadId() == 0) {
        std::this_thread::yield();
    }
    g_watchedAllocations = 0;
    g_watchedThread = capture.audioThreadId();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int percent = 100;
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        // 每 20 毫秒改一次：直通 ↔ 處理的切換與收回緩衝都會發生很多次
        percent = percent == 100 ? 55 : percent == 55 ? 0 : 100;
        engine.setVolume(percent);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(engine.isFinished());
    g_watchedThread = 0;
    engine.stop();
    CHECK(g_watchedAllocations.load() == 0);
}

TEST_CASE("即時路徑：同格式無縫接續下一首的期間，音訊執行緒不配置記憶體") {
    // 一次交出一首事先準備好的曲目
    class OneMore final : public auralis::core::NextTrackProvider {
    public:
        std::optional<auralis::core::EngineTrack> nextTrack() override {
            if (!next.source) {
                return std::nullopt;
            }
            return std::move(next);
        }
        auralis::core::EngineTrack next;
    };
    AudioFormat format;
    format.rate = 96000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    ProcessorChainImpl chain;
    chain.add(std::make_shared<HalfGainFactory>());
    PlaybackEngine engine(chain);
    OneMore provider;
    provider.next.source = std::make_unique<MemorySource>(format, makeNoise(format, 48000, 6));
    provider.next.token = 2;
    engine.setNextTrackProvider(&provider);
    CaptureBackend capture(fastCapture());
    auto source = std::make_unique<MemorySource>(format, makeNoise(format, 48000, 5));
    REQUIRE(engine.play(std::move(source), capture).hasValue());
    while (capture.audioThreadId() == 0) {
        std::this_thread::yield();
    }
    g_watchedAllocations = 0;
    g_watchedThread = capture.audioThreadId();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(engine.isFinished());
    g_watchedThread = 0;
    CHECK(engine.position().token == 2);
    engine.stop();
    CHECK(g_watchedAllocations.load() == 0);
}

TEST_CASE("即時路徑：WASAPI 獨占的音訊執行緒登記 MMCSS，播放中（含 24-in-24 打包）不配置記憶體") {
    auto device = std::make_shared<FakeDevice>();
    device->supports = supportsOnly({{96000, 24, 24}});
    WasapiBackend backend(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device),
                          device->id);
    AudioFormat format;
    format.rate = 96000;
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    ProcessorChainImpl chain;
    PlaybackEngine engine(chain);
    auto source = std::make_unique<MemorySource>(format, makeNoise(format, 48000, 5));
    REQUIRE(engine.play(std::move(source), backend).hasValue());
    while (backend.audioThreadId() == 0) {
        std::this_thread::yield();
    }
    g_watchedAllocations = 0;
    g_watchedThread = backend.audioThreadId();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!engine.isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(engine.isFinished());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    g_watchedThread = 0;
    engine.stop();
    CHECK(backend.mmcssRegistered());
    CHECK(g_watchedAllocations.load() == 0);
}
