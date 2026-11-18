// 檔案內容寫的路徑（播放清單的項目等）的安全檢查：網路路徑、裝置、協定前綴在查詢檔案系統之前就
// 拒絕；之後一層一層看目錄項目，碰到符號連結或 junction 就停，不查連結另一邊（連結可以指到網路
// 位置，查了會對外連線）。查檔的掛勾記下每一次查詢，用來證明被拒的路徑與連結另一邊沒有被查詢。
// 連結只建在測試暫存資料夾裡、只指到同一個暫存資料夾裡的東西。
#include "library_fixtures.h"

#include "platform/content_path_check.h"

#include <auralis/content_path.h>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QStringList>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

/// 記下 checkContentPath 查詢過的路徑。
struct ProbeLog {
    QStringList probed;
    ProbeLog() {
        setContentPathProbeHookForTest([this](const QString& path) {
            probed.append(path);
            return true;
        });
    }
    ~ProbeLog() { setContentPathProbeHookForTest({}); }
    ProbeLog(const ProbeLog&) = delete;
    ProbeLog& operator=(const ProbeLog&) = delete;
};

bool anyUnder(const QStringList& paths, const QString& folder) {
    for (const QString& path : paths) {
        if (path.startsWith(folder + QLatin1Char('\\'), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("T-63：一般的本機檔案可用，回傳正規化後的路徑；不存在的拒絕") {
    const QString dir = freshTestDir(QStringLiteral("content-path-plain"));
    const QString file = dir + QStringLiteral("\\專輯\\01.flac");
    writeBytes(file, "x");

    const ContentPathCheck ok = checkContentPath(dir + QStringLiteral("/專輯/./01.flac"));
    CHECK(ok.usable());
    CHECK(ok.path == file);

    const QString missing = dir + QStringLiteral("\\沒有\\02.flac");
    const ContentPathCheck notFound = checkContentPath(missing);
    CHECK(notFound.status == ContentPathStatus::NotFound);
    CHECK(notFound.path == missing);
    CHECK(checkContentPath(dir + QStringLiteral("\\專輯\\02.flac")).status ==
          ContentPathStatus::NotFound);
}

TEST_CASE("T-63：網路路徑、裝置路徑、裝置名稱、協定前綴在查詢檔案系統之前就拒絕") {
    struct Case {
        QString path;
        ContentPathStatus status;
    };
    const QList<Case> cases{
        {QStringLiteral("\\\\server\\share\\a.flac"), ContentPathStatus::NetworkPath},
        {QStringLiteral("//server/share/a.flac"), ContentPathStatus::NetworkPath},
        {QStringLiteral("\\\\?\\C:\\a.flac"), ContentPathStatus::DevicePath},
        {QStringLiteral("\\\\?\\UNC\\server\\share\\a.flac"), ContentPathStatus::DevicePath},
        {QStringLiteral("\\\\.\\C:\\a.flac"), ContentPathStatus::DevicePath},
        {QStringLiteral("\\??\\C:\\a.flac"), ContentPathStatus::DevicePath},
        {QStringLiteral("C:\\音樂\\CON"), ContentPathStatus::DeviceName},
        {QStringLiteral("C:\\音樂\\nul.flac"), ContentPathStatus::DeviceName},
        {QStringLiteral("C:\\COM1\\a.flac"), ContentPathStatus::DeviceName},
        {QStringLiteral("C:\\音樂\\lpt9 .txt"), ContentPathStatus::DeviceName},
        {QStringLiteral("http://example.com/a.flac"), ContentPathStatus::ProtocolPrefix},
        {QStringLiteral("file:///C:/a.flac"), ContentPathStatus::ProtocolPrefix},
        {QStringLiteral("smb:server/a.flac"), ContentPathStatus::ProtocolPrefix},
        {QStringLiteral("C:a.flac"), ContentPathStatus::InvalidPath},
        {QStringLiteral("a.flac"), ContentPathStatus::InvalidPath},
        {QStringLiteral("\\音樂\\a.flac"), ContentPathStatus::InvalidPath},
        {QStringLiteral("C:\\音樂\\a.flac:stream"), ContentPathStatus::InvalidPath},
        {QStringLiteral("C:\\音樂\\*.flac"), ContentPathStatus::InvalidPath},
        {QString(), ContentPathStatus::InvalidPath},
    };
    ProbeLog log;
    for (const Case& c : cases) {
        INFO(c.path.toStdString());
        const ContentPathCheck result = checkContentPath(c.path, QStringLiteral("C:\\音樂"));
        CHECK(result.status == c.status);
        CHECK(result.path == c.path);
    }
    CHECK(log.probed.isEmpty());
}

TEST_CASE("M-04 審查：路徑含控制字元（\\r、\\n、\\0、\\t、U+007F）是 InvalidPath，查詢之前就拒絕") {
    const QString dir = freshTestDir(QStringLiteral("content-path-control"));
    writeBytes(dir + QStringLiteral("\\a.flac"), "x");
    ProbeLog log;
    for (const char16_t c : {u'\r', u'\n', u'\0', u'\t', u'\x7f'}) {
        // 控制字元前面那一段是真的存在的檔案：截斷後查詢會查到它
        const QString path = dir + QStringLiteral("\\a.flac") + QChar(c) + QStringLiteral("b.flac");
        INFO(static_cast<int>(c));
        const ContentPathCheck result = checkContentPath(path, dir);
        CHECK(result.status == ContentPathStatus::InvalidPath);
        CHECK(result.path == path);
    }
    CHECK(log.probed.isEmpty());
}

TEST_CASE("S-3：上標數字的 COM¹–³、LPT¹–³ 與 CONIN$、CONOUT$ 也是裝置名稱，查詢之前就拒絕") {
    // Windows 11 把這幾個也當成裝置；GetFullPathNameW 不一定把它們換成 \\.\ 開頭
    const QStringList names{
        QStringLiteral("C:\\音樂\\COM¹"),      QStringLiteral("C:\\音樂\\com².flac"),
        QStringLiteral("C:\\COM³\\a.flac"),    QStringLiteral("C:\\音樂\\LPT¹.txt"),
        QStringLiteral("C:\\音樂\\lpt² "),     QStringLiteral("C:\\LPT³\\a.flac"),
        QStringLiteral("C:\\音樂\\CONIN$"),    QStringLiteral("C:\\音樂\\conout$.flac"),
        QStringLiteral("C:\\CONOUT$\\a.flac"),
    };
    ProbeLog log;
    for (const QString& path : names) {
        INFO(path.toStdString());
        const ContentPathCheck result = checkContentPath(path, QStringLiteral("C:\\音樂"));
        CHECK(result.status == ContentPathStatus::DeviceName);
        CHECK(result.path == path);
    }
    CHECK(log.probed.isEmpty());
    // 名稱只是含有這些字的照常查詢
    const QString dir = freshTestDir(QStringLiteral("content-path-device-like"));
    for (const QString& name : {QStringLiteral("COM¹ 精選.flac"), QStringLiteral("CONINX.flac"),
                                QStringLiteral("LPT10.flac")}) {
        writeBytes(dir + QLatin1Char('\\') + name, "x");
        CHECK(checkContentPath(dir + QLatin1Char('\\') + name).usable());
    }
}

TEST_CASE("T-63：中間一層是 junction 時拒絕，junction 另一邊沒有被查詢") {
    const LinkDir links(QStringLiteral("content-path-junction"));
    const QString outside = links.path + QStringLiteral("\\外面");
    writeBytes(outside + QStringLiteral("\\專輯\\a.flac"), "x");
    const QString junction = links.path + QStringLiteral("\\清單\\連到外面");
    makeJunction(junction, outside);

    ProbeLog log;
    const ContentPathCheck result = checkContentPath(junction + QStringLiteral("\\專輯\\a.flac"));
    CHECK(result.status == ContentPathStatus::LinkInPath);
    CHECK(result.path == junction);
    CHECK(log.probed.contains(junction));
    CHECK_FALSE(anyUnder(log.probed, junction));
    CHECK_FALSE(anyUnder(log.probed, outside));
    // 起點在 junction 上面時一樣要查到它
    const ContentPathCheck fromStart = checkContentPath(junction + QStringLiteral("\\專輯\\a.flac"),
                                                        links.path + QStringLiteral("\\清單"));
    CHECK(fromStart.status == ContentPathStatus::LinkInPath);
    // 直接寫外面的真實路徑可以用（連結本身才是問題）
    CHECK(checkContentPath(outside + QStringLiteral("\\專輯\\a.flac")).usable());
}

TEST_CASE("T-63：最後一層是符號連結時拒絕，不查它指到的檔案") {
    const LinkDir links(QStringLiteral("content-path-symlink"));
    const QString target = links.path + QStringLiteral("\\外面\\target.flac");
    writeBytes(target, "x");
    const QString link = links.path + QStringLiteral("\\清單\\link.flac");
    if (!makeSymlink(link, target, false)) {
        SKIP("沒有建立符號連結的權限（要開發人員模式或系統管理員）；"
             "junction 的情況由另一項測試涵蓋");
    }
    ProbeLog log;
    const ContentPathCheck result = checkContentPath(link);
    CHECK(result.status == ContentPathStatus::LinkInPath);
    CHECK(result.path == link);
    CHECK_FALSE(log.probed.contains(target));
}

TEST_CASE("T-63：使用者給的起點本身是 junction 時照常可用，起點以下仍檢查") {
    const LinkDir links(QStringLiteral("content-path-start"));
    const QString real = links.path + QStringLiteral("\\真的音樂");
    writeBytes(real + QStringLiteral("\\專輯\\a.flac"), "x");
    const QString start = links.path + QStringLiteral("\\音樂捷徑");
    makeJunction(start, real);

    const ContentPathCheck ok = checkContentPath(start + QStringLiteral("\\專輯\\a.flac"), start);
    CHECK(ok.usable());
    CHECK(ok.path == start + QStringLiteral("\\專輯\\a.flac"));
    CHECK(checkContentPath(start, start).usable());
    // 起點不是它的時候照樣擋
    CHECK(checkContentPath(start + QStringLiteral("\\專輯\\a.flac")).status ==
          ContentPathStatus::LinkInPath);
    CHECK(checkContentPath(start + QStringLiteral("\\專輯\\a.flac"),
                           links.path + QStringLiteral("\\別的"))
              .status == ContentPathStatus::LinkInPath);
    // 起點以下的 junction 照樣擋
    const QString inner = real + QStringLiteral("\\內層連結");
    makeJunction(inner, real + QStringLiteral("\\專輯"));
    CHECK(checkContentPath(start + QStringLiteral("\\內層連結\\a.flac"), start).status ==
          ContentPathStatus::LinkInPath);
}

TEST_CASE("T-63：雲端佔位檔這類不是連結的重新解析點照常可用") {
    const QString dir = freshTestDir(QStringLiteral("content-path-placeholder"));
    const QString marked = dir + QStringLiteral("\\placeholder.flac");
    writeBytes(marked, "x");
    if (!setCustomReparsePoint(marked)) {
        SKIP("這個檔案系統不能設定自訂的重新解析點");
    }
    CHECK(checkContentPath(marked).usable());
}

TEST_CASE("T-63：超過 260 字元的路徑可以檢查") {
    const QString dir = freshTestDir(QStringLiteral("content-path-long"));
    QString folder = dir;
    while (folder.size() < 300) {
        folder += QStringLiteral("\\很長的資料夾名稱很長的資料夾名稱很長的資料夾名稱");
    }
    const QString file = folder + QStringLiteral("\\a.flac");
    writeBytes(file, "x");
    REQUIRE(file.size() > 260);
    const ContentPathCheck result = checkContentPath(file);
    CHECK(result.usable());
    CHECK(result.path == file);
    CHECK(checkContentPath(folder + QStringLiteral("\\b.flac")).status ==
          ContentPathStatus::NotFound);
}
