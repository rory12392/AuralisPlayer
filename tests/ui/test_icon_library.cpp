// 圖示（介面規範 §6.2）：SVG 轉成路徑，而且每個圖示只由直線與正圓構成。
#include "shell/icon_library.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QRegularExpression>

using auralis::ui::IconLibrary;

namespace {

const QString kIconSourceDir = QStringLiteral(AURALIS_ICON_DIR);

} // namespace

TEST_CASE("圖示：path 與 circle 合成一條路徑，正圓轉成兩段等半徑的弧") {
    const QString path = IconLibrary::pathFromSvg(
        R"(<svg viewBox="0 0 24 24"><circle cx="10" cy="12" r="4"/><path d="M1 1 L2 2"/></svg>)");
    CHECK(path == QStringLiteral("M 6 12 A 4 4 0 1 0 14 12 A 4 4 0 1 0 6 12 Z M1 1 L2 2"));
}

TEST_CASE("圖示：直線與正圓以外的圖形元素一律拒絕") {
    CHECK(IconLibrary::pathFromSvg(R"(<svg><rect x="1" y="1" width="4" height="4"/></svg>)")
              .isEmpty());
    CHECK(
        IconLibrary::pathFromSvg(R"(<svg><ellipse cx="1" cy="1" rx="2" ry="1"/></svg>)").isEmpty());
    CHECK(IconLibrary::pathFromSvg("<svg><path d=").isEmpty());
}

TEST_CASE("圖示：每個 SVG 只用直線指令與正圓弧、不帶顏色、畫板 24×24") {
    const QStringList files =
        QDir(kIconSourceDir).entryList({QStringLiteral("*.svg")}, QDir::Files);
    REQUIRE(files.size() >= 22);
    // M-07（T-121）：全螢幕的深淺色切換鈕（DESIGN §6.2）
    CHECK(files.contains(QStringLiteral("contrast.svg")));
    // 只允許 M、L、H、V、Z（直線）與 A（弧，半徑要相等）；不允許曲線 C、Q、S、T
    const QRegularExpression forbidden(QStringLiteral("[CcQqSsTt]"));
    const QRegularExpression arc(QStringLiteral("A\\s*([\\d.]+)\\s+([\\d.]+)"));
    for (const QString& file : files) {
        INFO(file.toStdString());
        QFile svg(kIconSourceDir + QLatin1Char('/') + file);
        REQUIRE(svg.open(QIODevice::ReadOnly));
        const QByteArray content = svg.readAll();
        CHECK(content.contains("viewBox=\"0 0 24 24\""));
        CHECK_FALSE(content.contains("#"));
        const QString path = IconLibrary::pathFromSvg(content);
        REQUIRE_FALSE(path.isEmpty());
        CHECK_FALSE(forbidden.match(path).hasMatch());
        for (auto it = arc.globalMatch(path); it.hasNext();) {
            const auto match = it.next();
            CHECK(match.captured(1) == match.captured(2));
        }
    }
}
