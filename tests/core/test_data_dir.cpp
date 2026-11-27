// 資料夾保護：開發版沒指定資料夾時拒絕，不退回使用者的真實資料夾。
#include "settings/data_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>

using auralis::core::DataDirInputs;
using auralis::core::resolveDataDir;

namespace {
DataDirInputs base() {
    DataDirInputs inputs;
    inputs.exeDir = QStringLiteral("C:/App");
    inputs.localAppData = QStringLiteral("D:/LocalAppData");
    return inputs;
}
} // namespace

TEST_CASE("資料夾：開發版沒有 --data-dir 也沒有 AURALIS_DATA_DIR 時拒絕啟動") {
    auto inputs = base();
    inputs.devBuild = true;
    inputs.portableFlag = true; // 就算有 portable.flag 也不退回
    const auto result = resolveDataDir(inputs);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.contains(QStringLiteral("AURALIS_DATA_DIR")));
}

TEST_CASE("資料夾：--data-dir 優先於環境變數") {
    auto inputs = base();
    inputs.devBuild = true;
    inputs.commandLine = QStringLiteral("D:/cli");
    inputs.environment = QStringLiteral("D:/env");
    const auto result = resolveDataDir(inputs);
    REQUIRE(result);
    CHECK(result.value() == QStringLiteral("D:/cli"));
}

TEST_CASE("資料夾：開發版用 AURALIS_DATA_DIR") {
    auto inputs = base();
    inputs.devBuild = true;
    inputs.environment = QStringLiteral("D:/repo/.localdata/app");
    const auto result = resolveDataDir(inputs);
    REQUIRE(result);
    CHECK(result.value() == QStringLiteral("D:/repo/.localdata/app"));
}

TEST_CASE("資料夾：發佈版有 portable.flag 時用 exe 旁的 data") {
    auto inputs = base();
    inputs.portableFlag = true;
    const auto result = resolveDataDir(inputs);
    REQUIRE(result);
    CHECK(result.value() == QStringLiteral("C:/App/data"));
}

TEST_CASE("資料夾：發佈版一般安裝用 %LOCALAPPDATA%\\AuralisPlayer") {
    const auto result = resolveDataDir(base());
    REQUIRE(result);
    CHECK(result.value() == QStringLiteral("D:/LocalAppData/AuralisPlayer"));
}

TEST_CASE("AC-19-4：建立 QGuiApplication 之前從命令列取 --data-dir（讀介面縮放用）") {
    using auralis::core::dataDirArgument;
    CHECK(dataDirArgument({QStringLiteral("--data-dir"), QStringLiteral("D:/測試 資料")}) ==
          QStringLiteral("D:/測試 資料"));
    CHECK(dataDirArgument({QStringLiteral("--page"), QStringLiteral("settings"),
                           QStringLiteral("--data-dir=E:/x")}) == QStringLiteral("E:/x"));
    CHECK(dataDirArgument({QStringLiteral("song.flac")}).isEmpty());
    // 選項在最後、沒有值時當作沒給（之後的命令列解析會報錯）
    CHECK(dataDirArgument({QStringLiteral("--data-dir")}).isEmpty());
}
