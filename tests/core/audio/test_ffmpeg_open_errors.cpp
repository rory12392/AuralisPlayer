// FFmpeg 音源開不了檔時給使用者看的原因（AC-01-3「無法播放：<檔名>（原因）」的括號裡）是中文，
// FFmpeg 的英文訊息只寫進日誌。副檔名和內容不符（例：Wave64 存成 .wav）時說明偵測到的格式。
// 測試檔在測試時產生到 .localdata\tmp\ 底下。
#include "audio/ffmpeg_source.h"
#include "signal_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>

#include <mutex>
#include <string>

#include <windows.h>

using auralis::AudioFormat;
using auralis::core::FfmpegSourceFactory;
using auralis::test::encodeFile;
using auralis::test::makeNoise;

namespace {

QString caseDir(const char* name) {
    const QString dir =
        QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/ffmpeg-open-errors/") + name;
    QDir(dir).removeRecursively();
    REQUIRE(QDir().mkpath(dir));
    return dir;
}

void writeAudio(const QString& path, const char* muxer) {
    AudioFormat format;
    format.rate = 44100;
    format.bits = 16;
    format.container = 16;
    format.channels = 2;
    const QString why = encodeFile(path, "pcm_s16le", muxer, format, makeNoise(format, 4410, 3));
    INFO(why.toStdString());
    REQUIRE(why.isEmpty());
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

/// 開檔失敗的原因（std::string：失敗時測試輸出看得到文字）。
std::string reasonFor(const QString& path) {
    FfmpegSourceFactory factory;
    const auto source = factory.open(path);
    REQUIRE_FALSE(source.hasValue());
    return source.error().message.toStdString();
}

std::mutex g_messagesMutex;
QStringList g_messages;

void collectMessage(QtMsgType, const QMessageLogContext&, const QString& message) {
    const std::lock_guard lock(g_messagesMutex);
    g_messages.append(message);
}

/// 測試期間收下 Qt 的日誌訊息。
struct QtLogCapture {
    QtMessageHandler previous;
    QtLogCapture() {
        {
            const std::lock_guard lock(g_messagesMutex);
            g_messages.clear();
        }
        previous = qInstallMessageHandler(collectMessage);
    }
    ~QtLogCapture() { qInstallMessageHandler(previous); }
    QtLogCapture(const QtLogCapture&) = delete;
    QtLogCapture& operator=(const QtLogCapture&) = delete;
    QString all() const {
        const std::lock_guard lock(g_messagesMutex);
        return g_messages.join(QLatin1Char('\n'));
    }
};

} // namespace

TEST_CASE("AC-01-3：Wave64 存成 .wav 時原因寫明內容不是這個副檔名的格式，英文訊息只進日誌") {
    const QString dir = caseDir("w64-as-wav");
    const QString path = dir + QStringLiteral("/其實是 w64.wav");
    writeAudio(path, "w64");

    const QtLogCapture log;
    const std::string reason = reasonFor(path);
    CHECK(reason == "檔案內容不是 .wav 格式，可能是 Sony Wave64");
    // FFmpeg 的原文留在日誌裡，方便查問題
    CHECK(log.all().contains(QStringLiteral("Invalid argument")));

    SECTION("真正的 WAV 照常開啟") {
        const QString real = dir + QStringLiteral("/real.wav");
        writeAudio(real, "wav");
        FfmpegSourceFactory factory;
        CHECK(factory.open(real).hasValue());
    }
}

TEST_CASE("AC-01-3：內容是清單（不在支援格式內）的 .flac 也說明內容和副檔名不符") {
    const QString dir = caseDir("concat-as-flac");
    writeAudio(dir + QStringLiteral("/target.wav"), "wav");
    const QString path = dir + QStringLiteral("/evil.flac");
    writeBytes(path, "ffconcat version 1.0\nfile 'target.wav'\n");
    const std::string reason = reasonFor(path);
    INFO(reason);
    CHECK(reason.starts_with("檔案內容不是 .flac 格式，可能是 "));
}

TEST_CASE("AC-01-3：找不到檔案、檔案被鎖住、內容無法辨識時的原因是中文") {
    const QString dir = caseDir("common");

    SECTION("找不到檔案") {
        CHECK(reasonFor(dir + QStringLiteral("/不存在.flac")) == "找不到檔案");
    }
    SECTION("被其他程式以獨占方式開著") {
        const QString path = dir + QStringLiteral("/locked.wav");
        writeAudio(path, "wav");
        HANDLE lock =
            CreateFileW(QDir::toNativeSeparators(path).toStdWString().c_str(), GENERIC_READ, 0,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(lock != INVALID_HANDLE_VALUE);
        const std::string reason = reasonFor(path);
        CloseHandle(lock);
        CHECK(reason == "沒有讀取權限，或檔案正被其他程式使用");
    }
    SECTION("內容無法辨識") {
        const QString path = dir + QStringLiteral("/garbage.flac");
        writeBytes(path, QByteArray(4096, '\x5a'));
        CHECK(reasonFor(path) == "檔案已損毀，或裡面沒有音訊");
    }
    SECTION("空檔") {
        const QString path = dir + QStringLiteral("/empty.mp3");
        writeBytes(path, QByteArray());
        CHECK(reasonFor(path) == "檔案已損毀或格式不支援");
    }
}
