// 單一執行個體（AC-11-5）：程式已在執行時，再用命令列給的路徑交給已開的那一個。
#include "single_instance.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QUuid>

using auralis::app::SingleInstance;

namespace {

/// 每次測試用不同的資料夾名稱，不和開著的程式或上一輪搶同一個管道。
QString uniqueKey() {
    return QStringLiteral("C:\\t-85\\%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

/// 處理事件直到 done 或逾時。
bool waitUntil(const std::function<bool()>& done) {
    const QDeadlineTimer deadline(5000);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return done();
}

} // namespace

TEST_CASE("AC-11-5：同一個資料夾只有一個主要的執行個體，第二個把路徑交給它") {
    const QString key = uniqueKey();
    QObject receiver;
    QList<QStringList> received;
    {
        SingleInstance primary(key);
        REQUIRE(primary.isPrimary());

        // 第二個在主要的開始接收之前就送完並關掉（主視窗還在建立時），內容照樣收得到
        SingleInstance second(key);
        CHECK_FALSE(second.isPrimary());
        const QStringList paths{QStringLiteral("C:\\音樂\\Rё∀L.wav"),
                                QStringLiteral("\\\\nas\\music\\ＮieR：Automata")};
        CHECK(second.forward(paths));
        primary.listen(&receiver, [&received](const QStringList& got) { received.append(got); });
        REQUIRE(waitUntil([&received] { return received.size() == 1; }));
        CHECK(received.first() == paths);

        // 空清單＝只把視窗叫到前面；接著再送一次也收得到（管道可以重複使用）
        SingleInstance third(key);
        CHECK(third.forward({}));
        CHECK(third.forward({QStringLiteral("D:\\a.flac")}));
        REQUIRE(waitUntil([&received] { return received.size() == 3; }));
        CHECK(received.at(1).isEmpty());
        CHECK(received.at(2) == QStringList{QStringLiteral("D:\\a.flac")});

        // 資料夾寫法不同（大小寫、分隔符）也是同一個
        SingleInstance sameFolder(key.toLower().replace(QLatin1Char('\\'), QLatin1Char('/')));
        CHECK_FALSE(sameFolder.isPrimary());
        // 別的資料夾是另一個執行個體
        SingleInstance otherFolder(uniqueKey());
        CHECK(otherFolder.isPrimary());
    }
    // 主要的結束後，下一個啟動的變成主要的
    SingleInstance next(key);
    CHECK(next.isPrimary());
    // 沒有主要的執行個體可送時回報失敗，不會卡住
    SingleInstance lonely(uniqueKey());
    REQUIRE(lonely.isPrimary());
}
