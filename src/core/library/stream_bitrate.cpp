#include "library/stream_bitrate.h"

#include <memory>

extern "C" {
#include <libavformat/avformat.h>
}

namespace auralis::core {

namespace {

/// 和播放的 FFmpeg 音源同一份白名單：只用本機檔案協定、只收支援格式的 demuxer，
/// 會依內容再開其他路徑的 demuxer（concat、hls）在讀檔頭之前就被拒絕。
constexpr const char* kProtocolWhitelist = "file";
constexpr const char* kFormatWhitelist = "flac,wav,aiff,ape,wv,mp3,aac,mov,ogg";

struct FormatCloser {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};

} // namespace

bool isLossyCodec(const QString& codec) {
    for (const char* lossy : {"MP3", "AAC", "Opus", "Vorbis"}) {
        if (codec.compare(QLatin1String(lossy), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<int> ffmpegBitrateKbps(const QString& path) {
    AVFormatContext* raw = nullptr;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", kProtocolWhitelist, 0);
    av_dict_set(&options, "format_whitelist", kFormatWhitelist, 0);
    // FFmpeg 在 Windows 把 UTF-8 路徑轉成寬字元、唯讀開檔
    const QByteArray utf8Path = path.toUtf8();
    const int opened = avformat_open_input(&raw, utf8Path.constData(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) {
        return std::nullopt;
    }
    const std::unique_ptr<AVFormatContext, FormatCloser> context(raw);
    if (avformat_find_stream_info(raw, nullptr) < 0) {
        return std::nullopt;
    }
    const int stream = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    qint64 bitsPerSecond = stream >= 0 ? raw->streams[stream]->codecpar->bit_rate : 0;
    if (bitsPerSecond <= 0) {
        bitsPerSecond = raw->bit_rate;
    }
    if (bitsPerSecond <= 0) {
        return std::nullopt;
    }
    return static_cast<int>((bitsPerSecond + 500) / 1000);
}

std::optional<int> audioFileBitrateKbps(const QString& path, const QString& codec, qint64 fileBytes,
                                        qint64 durationMs) {
    if (isLossyCodec(codec)) {
        return ffmpegBitrateKbps(path);
    }
    if (fileBytes <= 0 || durationMs <= 0) {
        return std::nullopt;
    }
    // 位元組 × 8 ÷ 毫秒 ＝ kbps
    return static_cast<int>(fileBytes * 8 / durationMs);
}

} // namespace auralis::core
