// 搜尋與篩選經 SearchController 查真正的音樂庫（掃描測試時產生的音檔）：
// 比對的欄位與不分大小寫、全形半形（AC-09-2）、短的日文字（規格 §1 第 1 條）、篩選疊加（AC-09-3）、
// 篩選選項、QML 單例 LibrarySearch。
#include "lib_test_helpers.h"
#include "library_fixtures.h"
#include "search_controller.h"

#include <QQmlComponent>
#include <QQmlEngine>

#include <algorithm>
#include <memory>

using namespace auralis;
using namespace auralis::test;
using auralis::mod::lib::SearchController;

namespace {

/// 測試音樂庫：五首，涵蓋曲名、藝人、專輯藝人、專輯、作曲者與各種格式。
/// ctest 每個測試一個行程、同時跑，所以每個測試用自己的資料夾（name）。
QString sampleDatabase(const QString& name) {
    const QString dir = freshTestDir(QStringLiteral("t-20-") + name);
    const QString root = dir + QStringLiteral("\\音樂");
    writeFlac(root + QStringLiteral("\\NieR\\01.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("ＮｉｅＲ：Automata")},
               {QStringLiteral("ARTIST"), QStringLiteral("岡部啓一")},
               {QStringLiteral("ALBUMARTIST"), QStringLiteral("MONACA")},
               {QStringLiteral("ALBUM"), QStringLiteral("NieR：Automata Original Soundtrack")},
               {QStringLiteral("DATE"), QStringLiteral("2017")}});
    writeFlac(root + QStringLiteral("\\ミク\\02.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("初音ミクの消失")},
               {QStringLiteral("ARTIST"), QStringLiteral("cosMo@暴走P")},
               {QStringLiteral("ALBUM"), QStringLiteral("ベスト")},
               {QStringLiteral("DATE"), QStringLiteral("2008")}},
              96000, 24, 9600);
    writeWav(root + QStringLiteral("\\其他\\03.wav"),
             {{QStringLiteral("TITLE"), QStringLiteral("Wave Song")},
              {QStringLiteral("ARTIST"), QStringLiteral("Tester")},
              {QStringLiteral("ALBUM"), QStringLiteral("Mixed")},
              {QStringLiteral("COMPOSER"), QStringLiteral("Johann Sebastian Bach")},
              {QStringLiteral("DATE"), QStringLiteral("2017")}});
    writeDsf(root + QStringLiteral("\\其他\\04.dsf"),
             {{QStringLiteral("TITLE"), QStringLiteral("DSF Song")},
              {QStringLiteral("ARTIST"), QStringLiteral("Tester")},
              {QStringLiteral("ALBUM"), QStringLiteral("Mixed")}});
    writeFlac(root + QStringLiteral("\\其他\\05.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("Other")},
               {QStringLiteral("ARTIST"), QStringLiteral("MONACA")},
               {QStringLiteral("ALBUM"), QStringLiteral("Other Album")},
               {QStringLiteral("DATE"), QStringLiteral("2017")}},
              96000, 24, 9600);
    const QString db = dir + QStringLiteral("\\library.db");
    scanIntoDatabase(db, root);
    return db;
}

struct Fixture {
    // LibraryQueryImpl 只保存路徑的參考，路徑要活得比它久
    QString dbPath;
    core::LibraryQueryImpl library{dbPath};
    SearchController search{library};
    int ready = 0;

    explicit Fixture(const char* name) : dbPath(sampleDatabase(QString::fromLatin1(name))) {
        // 前提：五首都掃進資料庫了
        const auto all = library.tracks({});
        INFO((all ? QString() : all.error().message).toStdString());
        REQUIRE(all.hasValue());
        REQUIRE(all.value().size() == 5);
        ensureApp();
        QObject::connect(&search, &SearchController::resultsReady, [this] { ++ready; });
    }

    /// 等目前的查詢全部載入，回傳排序過的曲名。
    QStringList titles() {
        const int before = ready;
        REQUIRE(waitFor([&] { return ready > before && search.complete(); }));
        QStringList list;
        for (int i = 0; i < search.count(); ++i) {
            list.append(search.results()->track(i).title);
        }
        std::sort(list.begin(), list.end());
        return list;
    }
    QStringList find(const QString& text) {
        search.setText(text);
        return titles();
    }
};

} // namespace

TEST_CASE("AC-09-2：搜尋比對曲名、藝人、專輯藝人、專輯、作曲者，不分大小寫與全形半形") {
    Fixture fx("fields");
    CHECK(fx.find(QStringLiteral("nier")) == QStringList{QStringLiteral("ＮｉｅＲ：Automata")});
    CHECK(fx.find(QStringLiteral("岡部啓一")) == QStringList{QStringLiteral("ＮｉｅＲ：Automata")});
    CHECK(fx.find(QStringLiteral("monaca")) ==
          (QStringList{QStringLiteral("Other"), QStringLiteral("ＮｉｅＲ：Automata")}));
    CHECK(fx.find(QStringLiteral("ORIGINAL soundtrack")) ==
          QStringList{QStringLiteral("ＮｉｅＲ：Automata")});
    CHECK(fx.find(QStringLiteral("bach")) == QStringList{QStringLiteral("Wave Song")});
    CHECK(fx.find(QStringLiteral("ＴＥＳＴＥＲ")) ==
          (QStringList{QStringLiteral("DSF Song"), QStringLiteral("Wave Song")}));
    CHECK(fx.find(QStringLiteral("沒有這首")).isEmpty());
    CHECK(fx.search.error().isEmpty());
    CHECK(fx.search.elapsedMs() >= 0);
}

TEST_CASE("規格 §1 第 1 條：只打 1–2 個日文字也找得到（走 LIKE，不走 trigram）") {
    Fixture fx("short");
    CHECK(fx.find(QStringLiteral("初音")) == QStringList{QStringLiteral("初音ミクの消失")});
    CHECK(fx.find(QStringLiteral("消")) == QStringList{QStringLiteral("初音ミクの消失")});
    CHECK(fx.find(QStringLiteral("ミク 消失")) == QStringList{QStringLiteral("初音ミクの消失")});
}

TEST_CASE("AC-09-3：依藝人、專輯、年份、格式、取樣率篩選，可以疊加，也和搜尋字串同時生效") {
    Fixture fx("filters");
    fx.search.setFormat(QStringLiteral("WAV"));
    CHECK(fx.titles() == QStringList{QStringLiteral("Wave Song")});
    fx.search.setFormat(QStringLiteral("DSD64"));
    CHECK(fx.titles() == QStringList{QStringLiteral("DSF Song")});
    fx.search.setAlbum(QStringLiteral("Mixed"));
    CHECK(fx.titles() == QStringList{QStringLiteral("DSF Song")});
    CHECK(fx.search.filtersActive());

    fx.search.clearFilters();
    CHECK(fx.titles().size() == 5);
    CHECK_FALSE(fx.search.filtersActive());

    fx.search.setSampleRate(96000);
    CHECK(fx.titles() == (QStringList{QStringLiteral("Other"), QStringLiteral("Wave Song"),
                                      QStringLiteral("初音ミクの消失")}));
    fx.search.setYear(2017);
    CHECK(fx.titles() == (QStringList{QStringLiteral("Other"), QStringLiteral("Wave Song")}));
    fx.search.setFormat(QStringLiteral("FLAC"));
    CHECK(fx.titles() == QStringList{QStringLiteral("Other")});

    SECTION("藝人比對藝人或專輯藝人，加上搜尋字串") {
        fx.search.clearFilters();
        fx.titles();
        fx.search.setArtist(QStringLiteral("MONACA"));
        CHECK(fx.titles() ==
              (QStringList{QStringLiteral("Other"), QStringLiteral("ＮｉｅＲ：Automata")}));
        CHECK(fx.find(QStringLiteral("automata")) ==
              QStringList{QStringLiteral("ＮｉｅＲ：Automata")});
        fx.search.setYear(2008);
        CHECK(fx.titles().isEmpty());
    }
    SECTION("DSD 倍率和取樣率矛盾時直接是 0 首") {
        fx.search.setFormat(QStringLiteral("DSD64"));
        CHECK(fx.titles().isEmpty());
        CHECK(fx.search.error().isEmpty());
    }
    SECTION("清除搜尋與篩選") {
        fx.search.setText(QStringLiteral("other"));
        fx.titles();
        fx.search.clear();
        CHECK(fx.titles().size() == 5);
        CHECK(fx.search.text().isEmpty());
        CHECK(fx.search.sampleRate() == 0);
    }
}

TEST_CASE("AC-09-3：篩選列的選項是整個音樂庫出現過的值") {
    Fixture fx("options");
    bool changed = false;
    QObject::connect(&fx.search, &SearchController::optionsChanged, [&] { changed = true; });
    fx.search.loadOptions();
    REQUIRE(waitFor([&] { return changed; }));
    CHECK(fx.search.optionsLoaded());
    CHECK(fx.search.formatOptions() ==
          (QStringList{QStringLiteral("FLAC"), QStringLiteral("WAV"), QStringLiteral("DSD64")}));
    CHECK(fx.search.yearOptions() == (QVariantList{2017, 2008}));
    CHECK(fx.search.artistOptions().contains(QStringLiteral("MONACA")));
    CHECK(fx.search.artistOptions().contains(QStringLiteral("岡部啓一")));
    CHECK(fx.search.albumOptions().contains(QStringLiteral("Mixed")));
    const QVariantList rates = fx.search.sampleRateOptions();
    REQUIRE(rates.size() == 3);
    CHECK(rates.first().toMap().value(QStringLiteral("value")).toInt() == 44100);
    CHECK(rates.first().toMap().value(QStringLiteral("text")).toString() ==
          QStringLiteral("44.1 kHz"));
    CHECK(rates.last().toMap().value(QStringLiteral("value")).toInt() == 2822400);
}

TEST_CASE("QML 以單例 LibrarySearch 讀寫條件與結果（Auralis.Lib）") {
    Fixture fx("qml");
    SearchController::setInstance(&fx.search);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQml\nimport Auralis.Lib\nQtObject {\n"
                      "  property int count: LibrarySearch.count\n"
                      "  Component.onCompleted: LibrarySearch.text = 'bach'\n}",
                      QUrl(QStringLiteral("qrc:/t20/test.qml")));
    std::unique_ptr<QObject> object(component.create());
    REQUIRE(object != nullptr);
    CHECK(fx.titles() == QStringList{QStringLiteral("Wave Song")});
    CHECK(object->property("count").toInt() == 1);
    SearchController::setInstance(nullptr);
}
