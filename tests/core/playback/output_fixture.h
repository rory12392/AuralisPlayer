// PlaybackService 經 OutputRegistry 的測試輸出路徑（Capture 加上可以注入的裝置錯誤）：
// test_output_status.cpp 與 test_dsd_output_path.cpp 共用。只給測試的 .cpp include。
#pragma once

#include "../audio/engine_helpers.h"
#include "../audio/signal_writer.h"
#include "audio/wasapi_backend.h"
#include "playback/playback_service_impl.h"

#include <QCoreApplication>

#include <atomic>
#include <functional>
#include <map>

using auralis::AudioFormat;
using auralis::OutputError;
using auralis::OutputErrorKind;
using auralis::OutputStatus;
using auralis::PlaybackService;
using auralis::TrackRef;
using auralis::core::CaptureBackend;
using auralis::core::OutputRegistryImpl;
using auralis::core::PlaybackServiceImpl;
using auralis::core::ProcessorChainImpl;
using auralis::core::SourceRegistryImpl;
using auralis::test::fastCapture;
using auralis::test::makeNoise;
using auralis::test::MemorySource;
using auralis::test::startsWithThenSilence;
using State = PlaybackService::State;
using Choice = PlaybackServiceImpl::OutputChoice;

namespace {

AudioFormat cd() {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    return format;
}

AudioFormat floatFormat() {
    AudioFormat format = cd();
    format.bits = 32;
    format.container = 32;
    format.isFloat = true;
    return format;
}

/// 副檔名 .mem 的音源，格式由測試指定。
class MemoryFactory final : public auralis::ISourceFactory {
public:
    QString name() const override { return QStringLiteral("記憶體"); }
    QStringList extensions() const override { return {QStringLiteral("mem")}; }
    auralis::Result<std::unique_ptr<auralis::IAudioSource>> open(const QString& path) override {
        const auto& [format, data] = tracks.at(path);
        return std::unique_ptr<auralis::IAudioSource>(std::make_unique<MemorySource>(format, data));
    }
    std::map<QString, std::pair<AudioFormat, QByteArray>> tracks;
};

/// 一個測試裝置的設定：被佔用、播放中被拔除、不收浮點、路徑本身改變訊號。
struct DeviceState {
    bool busy = false;
    std::atomic<bool> lost{false};
    bool integerOnly = false;
    QString signalChange;
};

/// 包住 Capture 的後端：照 DeviceState 回報裝置錯誤與裝置格式。
class TestBackend final : public auralis::IOutputBackend {
public:
    explicit TestBackend(DeviceState& state) : m_state(state), m_capture(fastCapture()) {}

    QString name() const override { return QStringLiteral("測試輸出"); }
    auralis::Result<AudioFormat> open(const AudioFormat& requested) override {
        if (m_state.busy) {
            m_kind = OutputErrorKind::DeviceBusy;
            return auralis::Error{QStringLiteral("AUDCLNT_E_DEVICE_IN_USE"),
                                  static_cast<long>(0x8889000A), QStringLiteral("Initialize")};
        }
        m_kind = OutputErrorKind::Other;
        auto opened = m_capture.open(requested);
        if (opened && m_state.integerOnly && requested.isFloat) {
            AudioFormat device = requested;
            device.isFloat = false;
            return device;
        }
        return opened;
    }
    auralis::Result<bool> start(auralis::IRenderSource& source) override {
        return m_capture.start(source);
    }
    void stop() noexcept override { m_capture.stop(); }
    void close() noexcept override { m_capture.close(); }
    OutputErrorKind lastErrorKind() const noexcept override { return m_kind; }
    std::optional<OutputError> playbackError() const override {
        if (!m_state.lost.load()) {
            return std::nullopt;
        }
        return OutputError{OutputErrorKind::DeviceLost,
                           auralis::Error{QStringLiteral("AUDCLNT_E_DEVICE_INVALIDATED"),
                                          static_cast<long>(0x88890004),
                                          QStringLiteral("GetBuffer")}};
    }
    QString signalChange() const override { return m_state.signalChange; }

    const CaptureBackend& capture() const { return m_capture; }

private:
    DeviceState& m_state;
    CaptureBackend m_capture;
    OutputErrorKind m_kind = OutputErrorKind::Other;
};

/// 一個輸出路徑，裝置 id 對到 DeviceState；記下最後建立的後端。
class TestFactory final : public auralis::IOutputBackendFactory {
public:
    explicit TestFactory(QString name) : m_name(std::move(name)) {}
    QString name() const override { return m_name; }
    QList<auralis::OutputDevice> devices() override {
        QList<auralis::OutputDevice> list;
        for (const auto& [id, state] : states) {
            list.append({id, id + QStringLiteral(" 裝置")});
        }
        return list;
    }
    auralis::Result<std::unique_ptr<auralis::IOutputBackend>>
    create(const QString& deviceId) override {
        auto backend = std::make_unique<TestBackend>(*states.at(deviceId));
        last = backend.get();
        return std::unique_ptr<auralis::IOutputBackend>(std::move(backend));
    }
    std::map<QString, std::unique_ptr<DeviceState>> states;
    TestBackend* last = nullptr;

private:
    QString m_name;
};

struct Fixture {
    Fixture() {
        if (QCoreApplication::instance() == nullptr) {
            static int argc = 1;
            static char name[] = "auralis_core_tests";
            static char* argv[] = {name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        for (const char* id : {"a", "b"}) {
            factory->states[QString::fromLatin1(id)] = std::make_unique<DeviceState>();
        }
        outputs.add(factory);
    }

    TrackRef add(const QString& name, double seconds, AudioFormat format = cd()) {
        const QString path = name + QStringLiteral(".mem");
        sourceFactory->tracks[path] = {
            format, makeNoise(format, static_cast<int>(format.rate * seconds), 7)};
        return TrackRef{path, 0};
    }
    DeviceState& device(const char* id) { return *factory->states.at(QString::fromLatin1(id)); }
    Choice choice(const char* id) const {
        return {QStringLiteral("測試路徑"), QString::fromLatin1(id), QString::fromLatin1(id)};
    }

    bool drive(const std::function<bool()>& condition, int timeoutMs = 20000) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!condition() && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents();
            service.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        QCoreApplication::processEvents();
        return condition();
    }

    std::shared_ptr<MemoryFactory> sourceFactory = std::make_shared<MemoryFactory>();
    std::shared_ptr<TestFactory> factory =
        std::make_shared<TestFactory>(QStringLiteral("測試路徑"));
    SourceRegistryImpl sources{sourceFactory};
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors, 17};
};

} // namespace
