// 來源鏈（AC-15-1 的前三段）與 .lrc 的編碼（AC-15-4）：實際寫檔，經核心的 TextDecoder 讀回。
#include "lyr_test_helpers.h"
#include "lyrics_finder.h"

#include <auralis/text_decoder.h>

#include <catch2/catch_test_macros.hpp>

using namespace auralis;
using namespace auralis::mod::lyr;
using namespace auralis::test;
using namespace Qt::Literals::StringLiterals;

namespace {

/// 一般曲目的來源鏈（.lrc → 內嵌），和模組裝的一樣。
LyricsFinder standardFinder(const LibraryQuery& library) {
    LyricsFinder finder;
    finder.append(std::make_unique<LrcFileStep>());
    finder.append(std::make_unique<EmbeddedStep>(library));
    return finder;
}

LyricsDocument findOrFail(const LyricsFinder& finder, const LyricsTrack& track) {
    const auto doc = finder.find(track, [] { return false; });
    REQUIRE(doc.has_value());
    return *doc;
}

LyricsTrack plainTrack(const QString& audioPath) {
    LyricsTrack t;
    t.ref = {audioPath, 0};
    t.audioPath = audioPath;
    return t;
}

/// 記下被呼叫幾次的來源（模擬之後加在最後的 LRCLIB）。
class CountingStep final : public LyricsStep {
public:
    explicit CountingStep(std::optional<LyricsDocument> result) : m_result(std::move(result)) {}
    LyricsSource kind() const override { return LyricsSource::Lrclib; }
    std::optional<LyricsDocument> find(const LyricsTrack&) override {
        ++calls;
        return m_result;
    }
    int calls = 0;

private:
    std::optional<LyricsDocument> m_result;
};

} // namespace

TEST_CASE("AC-15-1：來源順序 同名 .lrc → 內嵌同步 → 內嵌純文字 → 沒有") {
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"歌曲.flac"_s);
    const QString lrc = dir->filePath(u"歌曲.lrc"_s);
    LyrLibrary library;
    const LyricsFinder finder = standardFinder(library);

    writeFile(lrc, "[00:01.00]from lrc\n");
    library.setEmbedded(audio, u"[00:02.00]from embedded"_s);
    LyricsDocument doc = findOrFail(finder, plainTrack(audio));
    CHECK(doc.source == LyricsSource::Lrc);
    CHECK(doc.lines.first().text == u"from lrc"_s);

    REQUIRE(QFile::remove(lrc));
    doc = findOrFail(finder, plainTrack(audio));
    CHECK(doc.source == LyricsSource::Embedded);
    CHECK(doc.synced);
    CHECK(doc.lines.first().timeMs == 2000);

    library.setEmbedded(audio, u"沒有時間標籤\n第二行"_s);
    doc = findOrFail(finder, plainTrack(audio));
    CHECK(doc.source == LyricsSource::Embedded);
    CHECK_FALSE(doc.synced);
    CHECK(doc.lines.size() == 2);

    library.setEmbedded(audio, QString());
    doc = findOrFail(finder, plainTrack(audio));
    CHECK(doc.status == LyricsDocument::Status::NotFound);
    CHECK(doc.source == LyricsSource::Embedded);
}

TEST_CASE("AC-15-1：.lrc 是空的或只有標頭時往下一段找") {
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"a.flac"_s);
    writeFile(dir->filePath(u"a.lrc"_s), "[ti:only header]\n");
    LyrLibrary library;
    library.setEmbedded(audio, u"內嵌"_s);
    CHECK(findOrFail(standardFinder(library), plainTrack(audio)).source == LyricsSource::Embedded);
}

TEST_CASE("AC-15-4：Big5、Shift-JIS、UTF-8（含 BOM）的 .lrc 都不亂碼，offset 照樣套用") {
    struct Case {
        const char* sample;
        unsigned codePage; // 0 表示 UTF-8 加 BOM
        const char* firstLine;
        qint64 firstTime;
    };
    for (const Case c : {Case{"zh-source.lrc", 950, "晴天的午後走過熟悉的街角", 500},
                         Case{"ja-source.lrc", 932, "夜明けの街を歩く 静かな朝の光", 1250},
                         Case{"zh-source.lrc", 0, "晴天的午後走過熟悉的街角", 500}}) {
        CAPTURE(c.sample, c.codePage);
        const auto dir = lyrTempDir();
        const QByteArray utf8 = lyrSample(QString::fromLatin1(c.sample));
        const QByteArray bytes =
            c.codePage == 0 ? QByteArray("\xEF\xBB\xBF") + utf8 : toCodePage(utf8, c.codePage);
        REQUIRE(bytes != utf8);
        writeFile(dir->filePath(u"track.lrc"_s), bytes);
        LyrLibrary library;
        const LyricsDocument doc =
            findOrFail(standardFinder(library), plainTrack(dir->filePath(u"track.flac"_s)));
        CHECK(doc.source == LyricsSource::Lrc);
        CHECK(doc.lines.size() == 5);
        CHECK(doc.lines.first().text == QString::fromUtf8(c.firstLine));
        CHECK(doc.lines.first().timeMs == c.firstTime);
    }
}

TEST_CASE("AC-15-1：CUE 曲目的 .lrc 候選是「整軌主檔名 軌號」再退回整軌同名") {
    LyricsTrack plain = plainTrack(u"D:\\音樂\\專輯\\01 歌.flac"_s);
    CHECK(lrcCandidates(plain) == QStringList{u"D:\\音樂\\專輯\\01 歌.lrc"_s});

    LyricsTrack cue;
    cue.ref = {u"D:\\音樂\\專輯\\Album.cue"_s, 3};
    cue.cueTrack = 3;
    cue.audioPath = u"D:\\音樂\\專輯\\Album.flac"_s;
    CHECK(lrcCandidates(cue) ==
          QStringList{u"D:\\音樂\\專輯\\Album 03.lrc"_s, u"D:\\音樂\\專輯\\Album.lrc"_s});
    cue.cueTrack = 12;
    CHECK(lrcCandidates(cue).first() == u"D:\\音樂\\專輯\\Album 12.lrc"_s);

    cue.audioPath.clear(); // 取不到整軌檔：沒有候選
    CHECK(lrcCandidates(cue).isEmpty());
}

TEST_CASE("AC-15-1：CUE 曲目有自己的 .lrc 時照原時間，借用整軌 .lrc 時換成曲目內的時間") {
    const auto dir = lyrTempDir();
    LyricsTrack cue;
    cue.ref = {dir->filePath(u"Album.cue"_s), 2};
    cue.cueTrack = 2;
    cue.audioPath = dir->filePath(u"Album.flac"_s);
    cue.startMs = 60'000;
    cue.endMs = 120'000;
    LyrLibrary library;
    const LyricsFinder finder = standardFinder(library);

    writeFile(dir->filePath(u"Album.lrc"_s),
              "[00:30.00]track 1\n[01:05.00]track 2 a\n[01:50.00]track 2 b\n[02:10.00]track 3\n");
    LyricsDocument doc = findOrFail(finder, cue);
    REQUIRE(doc.lines.size() == 2);
    CHECK(doc.lines[0] == LyricLine{5000, u"track 2 a"_s});
    CHECK(doc.lines[1] == LyricLine{50'000, u"track 2 b"_s});

    writeFile(dir->filePath(u"Album 02.lrc"_s), "[00:01.00]own lrc\n");
    doc = findOrFail(finder, cue);
    CHECK(doc.lines == QList<LyricLine>{{1000, u"own lrc"_s}});
}

TEST_CASE("AC-15-1：來源清單可以在最後插入一段，前面找到時不會叫到它") {
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"x.flac"_s);
    LyrLibrary library;
    LyricsFinder finder = standardFinder(library);
    LyricsDocument online;
    online.status = LyricsDocument::Status::Found;
    online.source = LyricsSource::Lrclib;
    online.lines = {{-1, u"線上"_s}};
    auto step = std::make_unique<CountingStep>(online);
    CountingStep* last = step.get();
    finder.append(std::move(step));

    CHECK(findOrFail(finder, plainTrack(audio)).source == LyricsSource::Lrclib);
    CHECK(last->calls == 1);
    library.setEmbedded(audio, u"內嵌"_s);
    CHECK(findOrFail(finder, plainTrack(audio)).source == LyricsSource::Embedded);
    CHECK(last->calls == 1);
}

TEST_CASE("AC-15-1：內嵌歌詞讀取失敗時照樣往下一段找，最後沒找到時是 Error（不是 notfound）") {
    const auto dir = lyrTempDir();
    const QString audio = dir->filePath(u"locked.flac"_s);
    LyrLibrary library;
    library.setEmbedded(audio, u"內嵌"_s);
    LyricsFinder finder = standardFinder(library);
    auto step = std::make_unique<CountingStep>(std::nullopt);
    CountingStep* last = step.get();
    finder.append(std::move(step));

    library.failEmbedded(audio, 1);
    const LyricsDocument failed = findOrFail(finder, plainTrack(audio));
    CHECK(last->calls == 1); // 讀取失敗不擋住後面的段落
    CHECK(failed.status == LyricsDocument::Status::Error);
    CHECK(failed.source == LyricsSource::Lrclib);

    const LyricsDocument next = findOrFail(finder, plainTrack(audio));
    CHECK(next.status == LyricsDocument::Status::Found);
    CHECK(next.source == LyricsSource::Embedded);
}

TEST_CASE("換歌時取消：被取消就停下，不回傳不完整的結果") {
    LyrLibrary library;
    const LyricsFinder finder = standardFinder(library);
    CHECK_FALSE(finder.find(plainTrack(u"C:\\none\\x.flac"_s), [] { return true; }).has_value());
    CHECK(library.embeddedCalls == 0);
}

TEST_CASE("風險：.lrc 太大、是資料夾、內容是二進位時不當掉，往下一段找") {
    const auto dir = lyrTempDir();
    LyrLibrary library;
    const LyricsFinder finder = standardFinder(library);

    const QString big = dir->filePath(u"big.flac"_s);
    {
        QFile file(dir->filePath(u"big.lrc"_s));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.resize(TextDecoder::kMaxFileBytes + 1)); // 超過核心的上限，decodeFile 回錯誤
    }
    library.setEmbedded(big, u"內嵌"_s);
    CHECK(findOrFail(finder, plainTrack(big)).source == LyricsSource::Embedded);

    const QString folder = dir->filePath(u"folder.flac"_s);
    REQUIRE(QDir(dir->path()).mkdir(u"folder.lrc"_s));
    CHECK(findOrFail(finder, plainTrack(folder)).status == LyricsDocument::Status::NotFound);

    const QString binary = dir->filePath(u"bin.flac"_s);
    QByteArray noise(256 * 1024, '\0');
    for (int i = 0; i < noise.size(); ++i) {
        noise[i] = static_cast<char>((i * 7919) & 0xFF);
    }
    writeFile(dir->filePath(u"bin.lrc"_s), noise);
    CHECK(findOrFail(finder, plainTrack(binary)).source == LyricsSource::Lrc);
}

TEST_CASE("CUE 曲目的區段由音樂庫的 CD 影格換成毫秒，整軌檔路徑由 audioFilePath 取得") {
    LyrLibrary library;
    library.addCueTrack(u"D:\\a\\Album.cue"_s, 2, 4500, 9000, u"D:\\a\\Album.flac"_s); // 60–120 秒
    const LyricsTrack cue = resolveTrack(library, {u"D:\\a\\Album.cue"_s, 2});
    CHECK(cue.cueTrack == 2);
    CHECK(cue.startMs == 60'000);
    CHECK(cue.endMs == 120'000);
    CHECK(cue.audioPath == u"D:\\a\\Album.flac"_s);
    CHECK(lrcCandidates(cue) == QStringList{u"D:\\a\\Album 02.lrc"_s, u"D:\\a\\Album.lrc"_s});

    library.addCueTrack(u"D:\\a\\Album.cue"_s, 3, 9000, -1); // 最後一軌到檔尾；取不到整軌檔
    const LyricsTrack last = resolveTrack(library, {u"D:\\a\\Album.cue"_s, 3});
    CHECK(last.endMs == -1);
    CHECK(last.audioPath.isEmpty());
    CHECK(lrcCandidates(last).isEmpty());

    const LyricsTrack plain = resolveTrack(library, {u"D:\\a\\b.flac"_s, 0});
    CHECK(plain.audioPath == u"D:\\a\\b.flac"_s);
    CHECK(plain.cueTrack == 0);
    CHECK(plain.endMs == -1);
}
