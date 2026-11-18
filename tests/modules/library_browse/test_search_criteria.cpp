// 搜尋條件的換算：格式分組（codec＋dsd_rate）、條件 → TrackFilter、取樣率文字、篩選選項。
#include "search_criteria.h"

#include <catch2/catch_test_macros.hpp>

using namespace auralis;
using namespace auralis::mod::lib;

namespace {

TrackInfo track(const QString& codec, int rate, int dsdRate = 0) {
    TrackInfo t;
    t.codec = codec;
    t.sampleRate = rate;
    t.dsdRate = dsdRate;
    return t;
}

} // namespace

TEST_CASE("AC-09-3：格式以 codec 加 DSD 倍率分組（FLAC／WAV／DSD64…）") {
    CHECK(formatKey(track(QStringLiteral("FLAC"), 44100)) == QStringLiteral("FLAC"));
    CHECK(formatKey(track(QStringLiteral("WAV"), 96000)) == QStringLiteral("WAV"));
    CHECK(formatKey(track(QStringLiteral("DSD"), 2822400, 2822400)) == QStringLiteral("DSD64"));
    CHECK(formatKey(track(QStringLiteral("DSD"), 11289600, 11289600)) == QStringLiteral("DSD256"));
    CHECK(formatKey(QStringLiteral("DSD"), 0) == QStringLiteral("DSD"));
}

TEST_CASE("AC-09-3：條件換成 TrackFilter，篩選可以和搜尋字串疊加") {
    SearchCriteria c;
    c.text = QStringLiteral("nier");
    c.artist = QStringLiteral("MONACA");
    c.album = QStringLiteral("NieR");
    c.year = 2017;
    c.format = QStringLiteral("FLAC");
    c.sampleRate = 96000;
    const auto f = toTrackFilter(c);
    REQUIRE(f.has_value());
    CHECK(f->search == c.text);
    CHECK(f->artist == c.artist);
    CHECK(f->album == c.album);
    CHECK(f->year == 2017);
    CHECK(f->codec == QStringLiteral("FLAC"));
    CHECK(f->sampleRate == 96000);
    CHECK(f->limit == -1);

    SECTION("DSD 倍率同時限定 codec 與取樣率") {
        SearchCriteria dsd;
        dsd.format = QStringLiteral("DSD128");
        const auto g = toTrackFilter(dsd);
        REQUIRE(g.has_value());
        CHECK(g->codec == QStringLiteral("DSD"));
        CHECK(g->sampleRate == 5644800);
        dsd.sampleRate = 5644800; // 選了相同的取樣率不矛盾
        CHECK(toTrackFilter(dsd).has_value());
    }
    SECTION("DSD 倍率和另外選的取樣率矛盾時不用查") {
        SearchCriteria dsd;
        dsd.format = QStringLiteral("DSD64");
        dsd.sampleRate = 96000;
        CHECK_FALSE(toTrackFilter(dsd).has_value());
    }
}

TEST_CASE("取樣率的顯示文字") {
    CHECK(sampleRateText(44100) == QStringLiteral("44.1 kHz"));
    CHECK(sampleRateText(96000) == QStringLiteral("96 kHz"));
    CHECK(sampleRateText(176400) == QStringLiteral("176.4 kHz"));
    CHECK(sampleRateText(2822400) == QStringLiteral("2.8224 MHz"));
}

TEST_CASE("AC-09-3：篩選選項去重並排序，DSD 排在 PCM 格式之後") {
    TrackInfo a = track(QStringLiteral("WAV"), 96000);
    a.artist = QStringLiteral("beta");
    a.albumArtist = QStringLiteral("Alpha");
    a.album = QStringLiteral("B");
    a.year = 2008;
    TrackInfo b = track(QStringLiteral("DSD"), 5644800, 5644800);
    b.artist = QStringLiteral("alpha2");
    b.album = QStringLiteral("a");
    b.year = 2017;
    TrackInfo c = track(QStringLiteral("DSD"), 2822400, 2822400);
    TrackInfo d = track(QStringLiteral("FLAC"), 44100);
    d.artist = QStringLiteral("beta");
    FilterOptionsBuilder builder;
    builder.add({a, b});
    builder.add({c, d});
    const FilterOptions o = builder.finish();
    CHECK(o.artists ==
          QStringList{QStringLiteral("Alpha"), QStringLiteral("alpha2"), QStringLiteral("beta")});
    CHECK(o.albums == QStringList{QStringLiteral("a"), QStringLiteral("B")});
    CHECK(o.years == QList<int>{2017, 2008});
    CHECK(o.formats == QStringList{QStringLiteral("FLAC"), QStringLiteral("WAV"),
                                   QStringLiteral("DSD64"), QStringLiteral("DSD128")});
    CHECK(o.sampleRates == QList<int>{44100, 96000, 2822400, 5644800});
}
