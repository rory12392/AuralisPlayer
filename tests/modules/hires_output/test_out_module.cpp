// 進階輸出模組：註冊 DSF／DFF 音源、停用開關、經播放引擎的位元流、標籤與內嵌封面並存。
#include "dsd_fixtures.h"
#include "dsd_mode_controller.h"
#include "dsd_test_helpers.h"
#include "engine_helpers.h"

#include "audio/processor_chain_impl.h"
#include "covers/cover_key.h"
#include "covers/cover_service_impl.h"
#include "library/tag_reader.h"
#include "modules/module_manager.h"

#include <auralis/dsd.h>

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QImage>
#include <QSettings>

#include <taglib/attachedpictureframe.h>
#include <taglib/dsdifffile.h>
#include <taglib/dsffile.h>
#include <taglib/id3v2tag.h>

#include <algorithm>
#include <cstring>

using auralis::AudioFormat;
using auralis::DsdFormat;
using auralis::DsdSink;
using auralis::Result;
using auralis::core::CaptureBackend;
using auralis::core::CoreServices;
using auralis::core::ModuleManager;
using auralis::core::ModuleStatus;
using auralis::core::PlaybackEngine;
using auralis::core::ProcessorChainImpl;
using namespace auralis::test;

namespace {

/// 測試用的 DSD 包裝器：位元流原樣放進 32 位元容器（直通），用來比對音源交給引擎的內容。
class PassThroughSink final : public DsdSink {
public:
    QString name() const override { return QStringLiteral("直通（測試）"); }
    Result<AudioFormat> open(const DsdFormat& format) override {
        m_frameBytes = format.bytesPerFrame();
        AudioFormat out;
        out.rate = format.dsdRate / 32;
        out.bits = 32;
        out.container = 32;
        out.channels = format.channels;
        return out;
    }
    qint64 maxOutputFrames(qint64 dsdFrames) const noexcept override { return dsdFrames; }
    qint64 convert(const std::byte* dsd, qint64 dsdFrames, std::byte* out) noexcept override {
        std::memcpy(out, dsd, static_cast<std::size_t>(dsdFrames * m_frameBytes));
        return dsdFrames;
    }

private:
    int m_frameBytes = 0;
};

ModuleStatus::State outState(const ModuleManager& manager) {
    const auto& statuses = manager.statuses();
    const auto it = std::find_if(statuses.begin(), statuses.end(), [](const ModuleStatus& s) {
        return s.code == QStringLiteral("OUT");
    });
    REQUIRE(it != statuses.end());
    return it->state;
}

/// 曲目右鍵選單登記的區塊 id（依登記順序）。
QStringList trackMenuSectionIds(const CoreServices& services) {
    QStringList ids;
    for (const QVariant& section : services.ui.trackMenuSections()) {
        ids.append(section.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

QByteArray pngBytes() {
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, "PNG"));
    return bytes;
}

TagLib::ID3v2::AttachedPictureFrame* pictureFrame(const QByteArray& png) {
    auto* frame = new TagLib::ID3v2::AttachedPictureFrame; // 交給 TagLib 的標籤擁有
    frame->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
    frame->setMimeType("image/png");
    frame->setPicture(TagLib::ByteVector(png.constData(), static_cast<unsigned>(png.size())));
    return frame;
}

} // namespace

TEST_CASE("AC-04-1：進階輸出模組啟動後，.dsf 與 .dff 由本模組的音源開啟") {
    const QString dir = freshDsdDir(QStringLiteral("module-on"));
    CoreServices services;
    REQUIRE_FALSE(services.sources.supports(QStringLiteral("a.dsf")));
    ModuleManager manager(services, dir + QStringLiteral("\\settings.ini"));
    manager.startAll();
    REQUIRE(outState(manager) == ModuleStatus::State::Started);

    CHECK(services.sources.supports(QStringLiteral("D:\\音樂\\曲目.DSF")));
    CHECK(services.sources.supports(QStringLiteral("D:\\音樂\\曲目.dff")));
    DsfSpec spec;
    spec.rate = 5644800;
    spec.channelData = {randomBytes(4096, 41), randomBytes(4096, 42)};
    const auto opened =
        services.sources.open(writeFile(dir, QStringLiteral("曲目.dsf"), makeDsf(spec)));
    REQUIRE(opened.hasValue());
    CHECK(opened.value()->format().dsdRate == 5644800);
    CHECK(opened.value()->lengthFrames() == 1024);
    // T-88：曲目右鍵選單登記「計算 ReplayGain」的區塊
    CHECK(trackMenuSectionIds(services) == QStringList{QStringLiteral("r128")});
    manager.stopAll();
}

TEST_CASE(
    "AC-03-0：沒有 ASIO 外掛時模組不註冊「ASIO」輸出路徑，原生 DSD 停用並寫「需要 ASIO 外掛」") {
    const QString dir = freshDsdDir(QStringLiteral("module-asio"));
    CoreServices services; // 沒有載入任何外掛
    ModuleManager manager(services, dir + QStringLiteral("\\settings.ini"));
    manager.startAll();
    REQUIRE(outState(manager) == ModuleStatus::State::Started);
    CHECK(services.outputs.find(QStringLiteral("ASIO")) == nullptr);
    const auto* dsd = auralis::mod::out::DsdModeController::create(nullptr, nullptr);
    REQUIRE(dsd != nullptr);
    const QVariantMap native = dsd->options().at(1).toMap();
    CHECK_FALSE(native.value(QStringLiteral("enabled")).toBool());
    CHECK(native.value(QStringLiteral("reason")).toString() == QStringLiteral("需要 ASIO 外掛"));
    manager.stopAll();
}

TEST_CASE("停用：modules/OUT/enabled=false 時不註冊 DSD 音源，程式照常啟動") {
    const QString dir = freshDsdDir(QStringLiteral("module-off"));
    const QString ini = dir + QStringLiteral("\\settings.ini");
    {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setValue(QStringLiteral("modules/OUT/enabled"), false);
    }
    CoreServices services;
    ModuleManager manager(services, ini);
    REQUIRE_NOTHROW(manager.startAll());
    CHECK(outState(manager) == ModuleStatus::State::Disabled);
    CHECK_FALSE(services.sources.supports(QStringLiteral("a.dsf")));
    CHECK_FALSE(services.sources.supports(QStringLiteral("a.dff")));
    CHECK_FALSE(services.sources.open(QStringLiteral("a.dsf")).hasValue());
    // T-88：停用時曲目右鍵選單沒有「計算 ReplayGain」（沒有登記）
    CHECK(trackMenuSectionIds(services).isEmpty());
}

TEST_CASE("AC-04-1：DSF 與 DFF 經播放引擎送到後端的位元流，和檔案裡的 DSD 資料逐位元相同") {
    const QString dir = freshDsdDir(QStringLiteral("engine"));
    // 8192 框（DSD64 約 0.09 秒），DSF 是兩組區塊
    const QList<QByteArray> channels{randomBytes(8192 * 4, 51), randomBytes(8192 * 4, 52)};
    DsfSpec dsf;
    dsf.channelData = channels;
    DffSpec dff;
    dff.data = dffInterleave(channels);
    struct Row {
        QString path;
        QByteArray expected;
    };
    const QList<Row> rows{{writeFile(dir, QStringLiteral("a.dsf"), makeDsf(dsf)),
                           interleaveFrames(channels, 8192, true)},
                          {writeFile(dir, QStringLiteral("a.dff"), makeDff(dff)),
                           interleaveFrames(channels, 8192, false)}};
    for (const Row& row : rows) {
        DYNAMIC_SECTION(row.path.right(3).toStdString()) {
            ProcessorChainImpl chain;
            PlaybackEngine engine(chain);
            engine.setDsdSink(std::make_shared<PassThroughSink>());
            CaptureBackend capture(fastCapture());
            auto status = engine.play(openDsdOk(row.path), capture);
            REQUIRE(status.hasValue());
            CHECK(status.value().bitPerfect);
            CHECK(status.value().sourceFormat.dsdRate == 2822400);
            REQUIRE(waitFinished(engine));
            CHECK(engine.underruns() == 0);
            const QByteArray captured = capture.captured();
            REQUIRE(captured.size() >= row.expected.size());
            CHECK(captured.left(row.expected.size()) == row.expected);
        }
    }
}

TEST_CASE("AC-04-1：TagLib 寫入標籤與內嵌封面後，封面讀得到，DSD 資料照樣逐位元相同") {
    const QString dir = freshDsdDir(QStringLiteral("tagged"));
    const QList<QByteArray> channels{randomBytes(8192, 61), randomBytes(8192, 62)};
    const QByteArray png = pngBytes();

    SECTION("DSF（ID3v2 接在資料後面）") {
        DsfSpec spec;
        spec.channelData = channels;
        const QString path = writeFile(dir, QStringLiteral("a.dsf"), makeDsf(spec));
        {
            TagLib::DSF::File file(path.toStdWString().c_str());
            REQUIRE(file.isValid());
            file.tag()->setTitle(TagLib::String(L"封面測試"));
            file.tag()->addFrame(pictureFrame(png));
            REQUIRE(file.save());
        }
        const auto cover = auralis::core::readEmbeddedCover(path);
        REQUIRE(cover.hasValue());
        CHECK(cover.value() == png);
        const auto image =
            auralis::core::decodeCoverSource(auralis::core::embeddedCoverSource(path), 64);
        REQUIRE(image.hasValue());
        CHECK(image.value().size() == QSize(8, 6));
        const auto source = openDsdOk(path);
        CHECK(source->lengthFrames() == 2048);
        CHECK(readAll(*source) == interleaveFrames(channels, 2048, true));
    }
    SECTION("DFF（ID3 區塊）") {
        DffSpec spec;
        spec.data = dffInterleave(channels);
        const QString path = writeFile(dir, QStringLiteral("a.dff"), makeDff(spec));
        {
            TagLib::DSDIFF::File file(path.toStdWString().c_str());
            REQUIRE(file.isValid());
            file.ID3v2Tag(true)->setTitle(TagLib::String(L"封面測試"));
            file.ID3v2Tag(true)->addFrame(pictureFrame(png));
            REQUIRE(file.save());
        }
        const auto cover = auralis::core::readEmbeddedCover(path);
        REQUIRE(cover.hasValue());
        CHECK(cover.value() == png);
        const auto image =
            auralis::core::decodeCoverSource(auralis::core::embeddedCoverSource(path), 64);
        REQUIRE(image.hasValue());
        CHECK(image.value().size() == QSize(8, 6));
        const auto source = openDsdOk(path);
        CHECK(source->lengthFrames() == 2048);
        CHECK(readAll(*source) == interleaveFrames(channels, 2048, false));
    }
}
