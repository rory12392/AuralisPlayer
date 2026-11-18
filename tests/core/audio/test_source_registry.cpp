// 音源註冊表：副檔名對應、模組優先於核心、後註冊的模組優先。
#include "audio/source_registry_impl.h"

#include <catch2/catch_test_macros.hpp>

using auralis::Error;
using auralis::IAudioSource;
using auralis::ISourceFactory;
using auralis::Result;
using auralis::core::SourceRegistryImpl;

namespace {

/// 不真的開檔，open() 回傳帶著自己名字的 Error，用來辨認是哪個工廠被選中。
class NamedFactory final : public ISourceFactory {
public:
    NamedFactory(QString name, QStringList extensions)
        : m_name(std::move(name)), m_extensions(std::move(extensions)) {}
    QString name() const override { return m_name; }
    QStringList extensions() const override { return m_extensions; }
    Result<std::unique_ptr<IAudioSource>> open(const QString&) override {
        return Error{m_name, 0, QStringLiteral("open")};
    }

private:
    QString m_name;
    QStringList m_extensions;
};

QString chosen(const SourceRegistryImpl& registry, const QString& path) {
    const auto result = registry.open(path);
    REQUIRE_FALSE(result.hasValue());
    return result.error().message;
}

} // namespace

TEST_CASE("音源：依副檔名（不分大小寫）選工廠，沒有工廠接受時回傳錯誤") {
    SourceRegistryImpl registry(
        std::make_shared<NamedFactory>(QStringLiteral("core"), QStringList{"flac", "dsf"}));
    CHECK(registry.supports(QStringLiteral("D:\\音樂\\a.FLAC")));
    CHECK_FALSE(registry.supports(QStringLiteral("D:\\音樂\\a.xyz")));
    CHECK_FALSE(registry.supports(QStringLiteral("D:\\音樂\\沒有副檔名")));
    CHECK(chosen(registry, QStringLiteral("a.Flac")) == QStringLiteral("core"));
    CHECK(chosen(registry, QStringLiteral("a.xyz")).contains(QStringLiteral("不支援")));
}

TEST_CASE(
    "規格 §1 停用方式（T-49）：沒有 DSD 音源（進階輸出停用）時 .dsf／.dff 寫「進階輸出已停用」") {
    SourceRegistryImpl registry(
        std::make_shared<NamedFactory>(QStringLiteral("core"), QStringList{"flac"}));
    CHECK(chosen(registry, QStringLiteral("D:\\音樂\\a.dsf")) == QStringLiteral("進階輸出已停用"));
    CHECK(chosen(registry, QStringLiteral("D:\\音樂\\b.DFF")) == QStringLiteral("進階輸出已停用"));
    // 其他沒人收的格式照舊寫不支援
    CHECK(chosen(registry, QStringLiteral("a.xyz")).contains(QStringLiteral("不支援")));
    // 模組啟動、註冊了 DSD 音源時交給它
    registry.add(std::make_shared<NamedFactory>(QStringLiteral("out"), QStringList{"dsf", "dff"}));
    CHECK(chosen(registry, QStringLiteral("a.dsf")) == QStringLiteral("out"));
}

TEST_CASE("音源：同一個副檔名時，模組註冊的優先於核心，後註冊的模組優先") {
    SourceRegistryImpl registry(
        std::make_shared<NamedFactory>(QStringLiteral("core"), QStringList{"flac", "dsf"}));
    registry.add(std::make_shared<NamedFactory>(QStringLiteral("out"), QStringList{"dsf", "dff"}));
    CHECK(chosen(registry, QStringLiteral("a.dsf")) == QStringLiteral("out"));
    CHECK(chosen(registry, QStringLiteral("a.dff")) == QStringLiteral("out"));
    CHECK(chosen(registry, QStringLiteral("a.flac")) == QStringLiteral("core"));

    registry.add(std::make_shared<NamedFactory>(QStringLiteral("later"), QStringList{"dsf"}));
    CHECK(chosen(registry, QStringLiteral("a.dsf")) == QStringLiteral("later"));
    CHECK(chosen(registry, QStringLiteral("a.dff")) == QStringLiteral("out"));
    registry.add(nullptr); // 忽略
    CHECK(registry.supports(QStringLiteral("a.dff")));
}

TEST_CASE("音源：預設的核心工廠是 FFmpeg，涵蓋 10 種格式的副檔名、不接 DSD") {
    SourceRegistryImpl registry;
    for (const char* suffix :
         {"flac", "m4a", "wav", "aiff", "ape", "wv", "mp3", "aac", "opus", "ogg"}) {
        INFO(suffix);
        CHECK(registry.supports(QStringLiteral("a.") + QLatin1String(suffix)));
    }
    CHECK_FALSE(registry.supports(QStringLiteral("a.dsf")));
    CHECK_FALSE(registry.supports(QStringLiteral("a.dff")));
}
