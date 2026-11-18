// OutputRegistry、WASAPI 工廠與 AURALIS_HR_CHECK。
#include "fake_wasapi.h"

#include "audio/output_registry_impl.h"
#include "audio/wasapi_backend.h"

#include <auralis/result.h>

#include <catch2/catch_test_macros.hpp>

using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::Error;
using auralis::IOutputBackend;
using auralis::IOutputBackendFactory;
using auralis::OutputDevice;
using auralis::Result;
using auralis::core::OutputRegistryImpl;
using auralis::core::WasapiBackendFactory;
using auralis::core::WasapiMode;
using auralis::test::FakeDevice;
using auralis::test::FakeEndpoints;
using auralis::test::supportsOnly;

namespace {

class NamedFactory final : public IOutputBackendFactory {
public:
    NamedFactory(QString name, int tag) : m_name(std::move(name)), tag(tag) {}
    QString name() const override { return m_name; }
    QList<OutputDevice> devices() override { return {}; }
    Result<std::unique_ptr<IOutputBackend>> create(const QString&) override {
        return Error{QStringLiteral("測試用"), 0, QStringLiteral("NamedFactory::create")};
    }

    QString m_name;
    int tag;
};

class NullSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("測試"); }
    Result<auralis::AudioFormat> open(const DsdFormat&) override { return auralis::AudioFormat{}; }
    qint64 maxOutputFrames(qint64 frames) const noexcept override { return frames; }
    qint64 convert(const std::byte*, qint64, std::byte*) noexcept override { return 0; }
};

Result<int> checked(long hr) {
    AURALIS_HR_CHECK(hr);
    return 7;
}

} // namespace

TEST_CASE("OutputRegistry：核心先放 WASAPI 獨占與共享，模組的後端接在後面，同名取代") {
    OutputRegistryImpl withCore;
    const auto factories = withCore.factories();
    REQUIRE(factories.size() == 2);
    CHECK(factories[0]->name() == QStringLiteral("WASAPI 獨占"));
    CHECK(factories[1]->name() == QStringLiteral("WASAPI 共享"));

    OutputRegistryImpl registry(false);
    registry.add(std::make_shared<NamedFactory>(QStringLiteral("ASIO"), 1));
    registry.add(std::make_shared<NamedFactory>(QStringLiteral("其他"), 2));
    registry.add(std::make_shared<NamedFactory>(QStringLiteral("ASIO"), 3));
    registry.add(nullptr);
    REQUIRE(registry.factories().size() == 2);
    auto asio = std::dynamic_pointer_cast<NamedFactory>(registry.find(QStringLiteral("ASIO")));
    REQUIRE(asio);
    CHECK(asio->tag == 3);
    CHECK(registry.factories()[0] == asio);
    CHECK(registry.find(QStringLiteral("沒有")) == nullptr);
}

TEST_CASE("OutputRegistry：收 DSD 包裝器給播放引擎，nullptr 取消") {
    OutputRegistryImpl registry(false);
    CHECK(registry.dsdSink() == nullptr);
    auto sink = std::make_shared<NullSink>();
    registry.setDsdSink(sink);
    CHECK(registry.dsdSink() == sink);
    registry.setDsdSink(nullptr);
    CHECK(registry.dsdSink() == nullptr);
}

TEST_CASE("WASAPI 工廠：列出裝置、建立後端；裝置不存在時回傳錯誤") {
    auto device = std::make_shared<FakeDevice>();
    WasapiBackendFactory factory(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device));
    const auto devices = factory.devices();
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].id == device->id);
    CHECK(devices[0].name == device->name);
    auto backend = factory.create(device->id);
    REQUIRE(backend.hasValue());
    CHECK(backend.value()->name() == QStringLiteral("WASAPI 獨占"));
    CHECK_FALSE(factory.create(QStringLiteral("不存在")).hasValue());
}

TEST_CASE("WASAPI 工廠：唯讀查詢獨占模式支援的格式（不初始化裝置）") {
    auto device = std::make_shared<FakeDevice>();
    device->supports = supportsOnly({{44100, 16, 16}, {96000, 32, 24}});
    WasapiBackendFactory factory(WasapiMode::Exclusive, std::make_shared<FakeEndpoints>(device));
    auto formats = factory.probeExclusiveFormats(device->id);
    REQUIRE(formats.hasValue());
    CHECK(formats.value().contains(QStringLiteral("44100 Hz 16 bit 2ch")));
    CHECK(formats.value().contains(QStringLiteral("96000 Hz 24-in-32 bit 2ch")));
    CHECK(formats.value().size() == 2);
    CHECK(device->initialized.empty());
}

TEST_CASE("AURALIS_HR_CHECK：失敗的 HRESULT 轉成含 HRESULT 與呼叫文字的 Error，成功時往下走") {
    auto ok = checked(S_OK);
    REQUIRE(ok.hasValue());
    CHECK(ok.value() == 7);
    auto failed = checked(E_ACCESSDENIED);
    REQUIRE_FALSE(failed.hasValue());
    CHECK(failed.error().hresult == E_ACCESSDENIED);
    CHECK(failed.error().call == QStringLiteral("hr"));
    CHECK(failed.error().message.startsWith(QStringLiteral("HRESULT 0x80070005")));
    // WASAPI 的錯誤碼不在系統訊息表裡：至少有十六進位碼
    auto wasapi = checked(AUDCLNT_E_DEVICE_IN_USE);
    REQUIRE_FALSE(wasapi.hasValue());
    CHECK(wasapi.error().message.startsWith(QStringLiteral("HRESULT 0x8889000a")));
}
