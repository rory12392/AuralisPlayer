// FFmpeg 音源只開本機檔案：內容是 concat／HLS 清單的檔案不會讓 FFmpeg 再去開清單裡的路徑，
// 帶協定前綴的路徑在交給 FFmpeg 之前就拒絕。對外連線只有歌詞查詢，播放不能連到任何網路位置。
// 測試檔在測試時產生到 .localdata\tmp\ 底下；網址只用來確認被擋下，不會真的連線
// （建置的 FFmpeg 沒有網路協定，被拒的路徑也不會交給 FFmpeg）。
#include "audio/ffmpeg_source.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDir>
#include <QFile>

#include <cstdarg>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/log.h>
}

using auralis::AudioFormat;
using auralis::core::FfmpegSourceFactory;
using auralis::test::encodeFile;
using auralis::test::makeNoise;

namespace {

std::mutex g_logMutex;
std::vector<std::string> g_log;

void captureLog(void* avcl, int level, const char* fmt, va_list vl) {
    char line[1024] = {};
    int printPrefix = 1;
    av_log_format_line2(avcl, level, fmt, vl, line, sizeof(line), &printPrefix);
    const std::lock_guard lock(g_logMutex);
    g_log.emplace_back(line);
}

/// 測試期間把 FFmpeg 的所有日誌（含 DEBUG 的「Opening '…' for reading」）收下來。
/// 參照的檔案有沒有被開啟，看日誌裡有沒有出現它的檔名。
class LogCapture {
public:
    LogCapture() {
        const std::lock_guard lock(g_logMutex);
        g_log.clear();
        av_log_set_callback(captureLog);
    }
    ~LogCapture() { av_log_set_callback(av_log_default_callback); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::string all() const {
        const std::lock_guard lock(g_logMutex);
        std::string text;
        for (const auto& line : g_log) {
            text += line;
        }
        return text;
    }
    bool mentions(const char* text) const { return all().find(text) != std::string::npos; }
};

/// 每個案例自己的資料夾，開始前清掉上一輪殘留。
QString caseDir(const char* name) {
    const QString dir =
        QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/ffmpeg-protocol/") + name;
    QDir(dir).removeRecursively();
    REQUIRE(QDir().mkpath(dir));
    return dir;
}

/// 寫一個正常、可以播放的 WAV，當成清單裡參照的目標。
void writeTargetWav(const QString& path) {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    const QString why = encodeFile(path, "pcm_s16le", "wav", format, makeNoise(format, 4410, 7));
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
}

void writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(text) == text.size());
}

} // namespace

TEST_CASE("AC-01-3：內容是 ffconcat 清單的 .flac 開檔失敗，清單裡的本機檔案沒有被開啟") {
    const QString dir = caseDir("concat");
    writeTargetWav(dir + QStringLiteral("/concat-target.wav"));
    // 副檔名是 .flac，音樂庫會把它當音樂檔交給 FFmpeg；FFmpeg 從內容探測出 concat demuxer
    writeText(dir + QStringLiteral("/evil.flac"),
              "ffconcat version 1.0\nfile 'concat-target.wav'\n");

    const LogCapture log;
    FfmpegSourceFactory factory;
    const auto source = factory.open(dir + QStringLiteral("/evil.flac"));
    INFO(log.all());
    CHECK_FALSE(source.hasValue());
    CHECK_FALSE(log.mentions("concat-target.wav"));
    CHECK(log.mentions("not on whitelist"));
}

TEST_CASE("AC-01-3：HLS 清單（.m3u8）開檔失敗，清單裡的本機片段沒有被開啟") {
    const QString dir = caseDir("hls");
    writeTargetWav(dir + QStringLiteral("/hls-segment.wav"));
    writeText(dir + QStringLiteral("/evil.m3u8"),
              "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n"
              "#EXTINF:0.1,\nhls-segment.wav\n#EXT-X-ENDLIST\n");

    const LogCapture log;
    FfmpegSourceFactory factory;
    const auto source = factory.open(dir + QStringLiteral("/evil.m3u8"));
    INFO(log.all());
    CHECK_FALSE(source.hasValue());
    CHECK_FALSE(log.mentions("hls-segment.wav"));
    CHECK(log.mentions("not on whitelist"));
}

TEST_CASE("AC-01-3：HLS 清單寫的網址被白名單擋下") {
    const QString dir = caseDir("hls-http");
    writeText(dir + QStringLiteral("/remote.m3u8"),
              "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n"
              "#EXTINF:0.1,\nhttp://auralis-test.invalid/segment.wav\n#EXT-X-ENDLIST\n");

    const LogCapture log;
    FfmpegSourceFactory factory;
    const auto source = factory.open(dir + QStringLiteral("/remote.m3u8"));
    INFO(log.all());
    CHECK_FALSE(source.hasValue());
    CHECK_FALSE(log.mentions("auralis-test.invalid"));
    CHECK(log.mentions("not on whitelist"));
}

TEST_CASE("AC-01-3：FFmpeg 的巢狀協定（subfile）被協定白名單擋下，不讀檔案") {
    // subfile 不是「名稱:」的寫法，開檔前的前綴檢查不攔它；要靠交給 FFmpeg 的協定白名單擋
    const QString dir = caseDir("subfile");
    const QString target = dir + QStringLiteral("/subfile-target.wav");
    writeTargetWav(target);

    const LogCapture log;
    FfmpegSourceFactory factory;
    const auto source = factory.open(QStringLiteral("subfile,,start,0,end,0,,:") +
                                     QDir::toNativeSeparators(target));
    INFO(log.all());
    CHECK_FALSE(source.hasValue());
    CHECK(log.mentions("Protocol 'subfile' not on whitelist"));
}

TEST_CASE("AC-01-3：帶協定前綴的路徑在交給 FFmpeg 之前就拒絕") {
    const auto path = GENERATE(as<QString>{}, //
                               "http://auralis-test.invalid/a.flac",
                               "https://auralis-test.invalid/a.flac", //
                               "smb://auralis-test.invalid/share/a.flac",
                               "tcp://127.0.0.1:9/a.flac", //
                               "file:C:/auralis-test/a.flac", "concat:a.flac|b.flac",
                               "data:audio/flac;base64,AAAA", "ab:c.flac");

    const LogCapture log;
    FfmpegSourceFactory factory;
    const auto source = factory.open(path);
    INFO(path.toStdString());
    INFO(log.all());
    REQUIRE_FALSE(source.hasValue());
    CHECK(source.error().call == QStringLiteral("FfmpegSourceFactory::open"));
    CHECK(source.error().message.contains(QStringLiteral("本機檔案")));
    // FFmpeg 完全沒有碰到這個路徑
    CHECK(log.all().empty());
}

TEST_CASE("AC-01-4：磁碟機代號與正斜線的本機路徑照常開啟") {
    const QString dir = caseDir("local");
    const QString target = dir + QStringLiteral("/local-target.wav");
    writeTargetWav(target);

    FfmpegSourceFactory factory;
    CHECK(factory.open(QDir::fromNativeSeparators(target)).hasValue());
    CHECK(factory.open(QDir::toNativeSeparators(target)).hasValue());
}
