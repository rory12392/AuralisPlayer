// 記住輸出（US-02「設定頁的輸出裝置選擇」、AC-02-6 的設定頁部分）：選了的輸出記在 settings.ini 的
// core/ 段，重新啟動（新的 PlaybackServiceImpl）後沿用；記住的裝置不在時用預設裝置並說明。
// 輸出路徑是測試用的工廠（Capture 後端），不碰實體裝置。
#include "audio/capture_backend.h"
#include "settings/output_settings.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

using auralis::core::CaptureBackend;
using auralis::core::OutputRegistryImpl;
using auralis::core::PlaybackServiceImpl;
using auralis::core::ProcessorChainImpl;
using auralis::core::SourceRegistryImpl;
using Choice = PlaybackServiceImpl::OutputChoice;

namespace {

/// 一個輸出路徑：列出 devices 裡的裝置，建立不在清單裡的裝置時失敗（和 WASAPI 的工廠一樣）。
class ListFactory final : public auralis::IOutputBackendFactory {
public:
    ListFactory(QString name, QStringList ids) : m_name(std::move(name)), m_ids(std::move(ids)) {}
    QString name() const override { return m_name; }
    QList<auralis::OutputDevice> devices() override {
        QList<auralis::OutputDevice> list;
        for (const QString& id : m_ids) {
            list.append({id, id + QStringLiteral(" 裝置")});
        }
        return list;
    }
    auralis::Result<std::unique_ptr<auralis::IOutputBackend>>
    create(const QString& deviceId) override {
        if (!m_ids.contains(deviceId)) {
            return auralis::Error{QStringLiteral("找不到輸出裝置：%1").arg(deviceId), 0,
                                  QStringLiteral("ListFactory::create")};
        }
        created.append(deviceId);
        return std::unique_ptr<auralis::IOutputBackend>(std::make_unique<CaptureBackend>());
    }
    QStringList created;

private:
    QString m_name;
    QStringList m_ids;
};

/// 一次「程式執行」：自己的輸出登記與播放服務，裝置清單由測試指定。
struct Run {
    explicit Run(QStringList exclusive, QStringList shared = {}) {
        if (QCoreApplication::instance() == nullptr) {
            static int argc = 1;
            static char name[] = "auralis_core_tests";
            static char* argv[] = {name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        exclusiveFactory = std::make_shared<ListFactory>(QStringLiteral("測試獨占"), exclusive);
        sharedFactory = std::make_shared<ListFactory>(QStringLiteral("測試共享"), shared);
        outputs.add(exclusiveFactory);
        outputs.add(sharedFactory);
    }

    std::shared_ptr<ListFactory> exclusiveFactory;
    std::shared_ptr<ListFactory> sharedFactory;
    SourceRegistryImpl sources;
    OutputRegistryImpl outputs{false};
    ProcessorChainImpl processors;
    PlaybackServiceImpl service{sources, outputs, processors};
};

Choice choice(const QString& backend, const QString& id) {
    return {backend, id, id + QStringLiteral(" 裝置")};
}

QString settingsFile(const QTemporaryDir& dir) {
    return dir.filePath(QStringLiteral("settings.ini"));
}

} // namespace

TEST_CASE("AC-02-6：選了的輸出裝置與模式記在 core/ 段，重新啟動後沿用") {
    QTemporaryDir dir(QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/output-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = settingsFile(dir);
    {
        Run first({QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("b")});
        CHECK(auralis::core::savedOutput(path).backend.isEmpty());
        REQUIRE(auralis::core::selectOutput(
            first.service, path, choice(QStringLiteral("測試共享"), QStringLiteral("b"))));
        CHECK(first.service.outputChoice() ==
              choice(QStringLiteral("測試共享"), QStringLiteral("b")));
    }
    const QSettings raw(path, QSettings::IniFormat);
    CHECK(raw.value(QStringLiteral("core/outputBackend")).toString() == QStringLiteral("測試共享"));
    CHECK(raw.value(QStringLiteral("core/outputDevice")).toString() == QStringLiteral("b"));
    CHECK(raw.value(QStringLiteral("core/outputDeviceName")).toString() ==
          QStringLiteral("b 裝置"));

    // 重新啟動：新的服務套用記住的輸出，不是預設的第一個路徑的第一個裝置
    Run second({QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("b")});
    CHECK(auralis::core::restoreOutput(second.service, path).isEmpty());
    CHECK(second.service.outputChoice() == choice(QStringLiteral("測試共享"), QStringLiteral("b")));
    CHECK(second.sharedFactory->created == QStringList{QStringLiteral("b")});
    CHECK(second.exclusiveFactory->created.isEmpty());
}

TEST_CASE("AC-02-6：記住的裝置不在時用預設裝置、說明是哪一個，記住的值保留") {
    QTemporaryDir dir(QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/output-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = settingsFile(dir);
    {
        Run first({QStringLiteral("a"), QStringLiteral("b")});
        REQUIRE(auralis::core::selectOutput(
            first.service, path, choice(QStringLiteral("測試獨占"), QStringLiteral("b"))));
    }
    // 裝置 b 拔掉了
    Run second({QStringLiteral("a")});
    const QString notice = auralis::core::restoreOutput(second.service, path);
    CHECK(notice == QStringLiteral("記住的輸出裝置「b 裝置」（測試獨占）不在，已改用預設裝置。"));
    // 沒有選擇＝第一次播放時用預設（第一個輸出路徑的第一個裝置）
    CHECK(second.service.outputChoice() == Choice{});
    CHECK(auralis::core::savedOutput(path) ==
          choice(QStringLiteral("測試獨占"), QStringLiteral("b")));

    // 輸出路徑本身不在（例：進階輸出模組停用時的 ASIO）也一樣退回預設
    QSettings(path, QSettings::IniFormat)
        .setValue(QStringLiteral("core/outputBackend"), QStringLiteral("不存在的路徑"));
    Run third({QStringLiteral("a")});
    CHECK_FALSE(auralis::core::restoreOutput(third.service, path).isEmpty());
    CHECK(third.service.outputChoice() == Choice{});
}

TEST_CASE("AC-02-6：選不了的輸出不記，原本記住的保留；沒有記過時不動預設") {
    QTemporaryDir dir(QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/output-XXXXXX"));
    REQUIRE(dir.isValid());
    const QString path = settingsFile(dir);
    Run run({QStringLiteral("a")});
    CHECK(auralis::core::restoreOutput(run.service, path).isEmpty());
    CHECK(run.service.outputChoice() == Choice{});

    REQUIRE(auralis::core::selectOutput(run.service, path,
                                        choice(QStringLiteral("測試獨占"), QStringLiteral("a"))));
    CHECK_FALSE(auralis::core::selectOutput(
        run.service, path, choice(QStringLiteral("測試獨占"), QStringLiteral("z"))));
    CHECK(run.service.outputChoice() == choice(QStringLiteral("測試獨占"), QStringLiteral("a")));
    CHECK(auralis::core::savedOutput(path) ==
          choice(QStringLiteral("測試獨占"), QStringLiteral("a")));
}
