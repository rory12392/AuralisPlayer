// 隔離測試用的假模組：只連進 auralis_boundary_tests，不會出現在主程式。
// TESTOK 正常啟動並記錄生命週期；TESTFAIL 在 start() 丟出例外（故障注入）。
#include "fake_modules.h"

#include <auralis/job_runner.h>
#include <auralis/module.h>
#include <auralis/module_database.h>
#include <auralis/module_settings.h>

#include <stdexcept>

namespace auralis::test {

FakeModuleCounters& counters() {
    static FakeModuleCounters value;
    return value;
}

namespace {

class OkModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("TESTOK"), QStringLiteral("測試用正常模組"), QStringLiteral("0")};
    }
    void start(ModuleHost& host) override {
        ++counters().okStarted;
        // 確認拿得到核心服務的入口，且屬於自己
        counters().okHostCode = host.moduleCode();
        (void)host.jobs();
        host.settings().setValue(QStringLiteral("probe"), 1);
        try {
            const auto written = host.database().execute(QStringLiteral(
                "INSERT INTO playlists(name, created_at, updated_at) VALUES ('x', 0, 0)"));
            counters().okDatabase = written ? QStringLiteral("written") : QStringLiteral("error");
        } catch (const std::logic_error&) {
            counters().okDatabase = QStringLiteral("denied");
        }
    }
    void stop() noexcept override { ++counters().okStopped; }
};

class FailingModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("TESTFAIL"), QStringLiteral("測試用故障模組"), QStringLiteral("0")};
    }
    void start(ModuleHost&) override {
        ++counters().failStarted;
        throw std::runtime_error("故障注入：TESTFAIL 啟動失敗");
    }
    void stop() noexcept override { ++counters().failStopped; }
};

} // namespace
} // namespace auralis::test

AURALIS_REGISTER_MODULE(TESTOK, [] {
    return std::unique_ptr<auralis::IModule>(std::make_unique<auralis::test::OkModule>());
})
AURALIS_REGISTER_MODULE(TESTFAIL, [] {
    return std::unique_ptr<auralis::IModule>(std::make_unique<auralis::test::FailingModule>());
})
