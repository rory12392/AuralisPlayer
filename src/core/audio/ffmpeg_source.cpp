// FFmpeg 音源：工廠、開檔（協定與格式白名單）、決定交出的格式。
// 解碼與跳轉在 ffmpeg_source_decode.cpp。
#include "audio/ffmpeg_source.h"

#include "audio/ffmpeg_source_impl.h"

#include <QFileInfo>

extern "C" {
#include <libavutil/avstring.h>
}

Q_LOGGING_CATEGORY(lcFfmpeg, "auralis.audio")

namespace auralis::core {

namespace {

/// 只用本機檔案協定。demuxer 再開別的檔案時沿用同一份白名單，所以清單內容也連不到網路。
constexpr const char* kProtocolWhitelist = "file";
/// 只收支援格式的 demuxer（mov 涵蓋 m4a／mp4）。concat、hls 這類會依內容再開其他路徑的
/// demuxer 在讀檔頭之前就被拒絕；file 協定照樣能開 \\ 網路路徑，所以協定白名單擋不住它們。
constexpr const char* kFormatWhitelist = "flac,wav,aiff,ape,wv,mp3,aac,mov,ogg";

/// FFmpeg 把「名稱:」開頭（名稱是英數字與 + - .）的路徑當成協定網址；只有一個字母是磁碟機代號。
bool hasUrlScheme(const QString& path) {
    qsizetype n = 0;
    while (n < path.size()) {
        const QChar c = path.at(n);
        if (!(c.isDigit() || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'+' ||
              c == u'-' || c == u'.')) {
            break;
        }
        ++n;
    }
    return n >= 2 && n < path.size() && path.at(n) == u':';
}

/// 依解出來的第一個框決定交出的格式；不支援的樣本格式回傳 false。
bool describe(const AVFrame& frame, const AVCodecContext& codec, const AVCodecParameters& par,
              AudioFormat& out) {
    out.rate = static_cast<quint32>(frame.sample_rate);
    out.channels = static_cast<quint16>(frame.ch_layout.nb_channels);
    switch (static_cast<AVSampleFormat>(frame.format)) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        out.container = 16;
        out.bits = 8;
        return true;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        out.container = 16;
        out.bits = 16;
        return true;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P: {
        // 24 位元來源由 FFmpeg 左對齊放在 32 位元裡；有效位元數看解碼器回報的值
        int bits =
            codec.bits_per_raw_sample > 0 ? codec.bits_per_raw_sample : par.bits_per_raw_sample;
        if (bits <= 0 || bits > 32) {
            bits = 32;
        }
        out.container = 32;
        out.bits = static_cast<quint16>(bits);
        return true;
    }
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
        out.container = 32;
        out.bits = 32;
        out.isFloat = true;
        return true;
    default:
        return false;
    }
}

/// FFmpeg 的錯誤碼 → 給使用者看的原因（「無法播放：<檔名>（原因）」的括號裡，AC-01-3）。
QString ffmpegReason(int code) {
    switch (code) {
    case AVERROR(ENOENT):
        return QStringLiteral("找不到檔案");
    case AVERROR(EACCES):
    case AVERROR(EPERM):
        return QStringLiteral("沒有讀取權限，或檔案正被其他程式使用");
    case AVERROR_INVALIDDATA:
    case AVERROR_EOF:
        return QStringLiteral("檔案已損毀或格式不支援");
    case AVERROR(EIO):
        return QStringLiteral("讀取檔案時發生錯誤");
    case AVERROR(ENOMEM):
        return QStringLiteral("記憶體不足");
    case AVERROR_STREAM_NOT_FOUND:
        // 內容無法辨識、只靠副檔名猜格式的檔案也會走到這裡
        return QStringLiteral("檔案已損毀，或裡面沒有音訊");
    default: // 格式不在白名單（EINVAL）、沒有對應的解碼器等
        return QStringLiteral("不支援的格式");
    }
}

/// 開檔被格式白名單拒絕時，只探測內容是什麼格式（不執行那個格式的 demuxer，所以內容是清單
/// 也不會再開別的路徑）。探測到白名單以外的格式時回傳它的名稱，其他情況空字串。
QString foreignFormatName(const QByteArray& utf8Path) {
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", kProtocolWhitelist, 0);
    AVIOContext* io = nullptr;
    const int opened = avio_open2(&io, utf8Path.constData(), AVIO_FLAG_READ, nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) {
        return {};
    }
    const AVInputFormat* format = nullptr;
    const int probed = av_probe_input_buffer2(io, &format, utf8Path.constData(), nullptr, 0, 0);
    avio_closep(&io);
    if (probed < 0 || format == nullptr || av_match_list(format->name, kFormatWhitelist, ',') > 0) {
        return {};
    }
    return QString::fromUtf8(format->long_name != nullptr ? format->long_name : format->name);
}

} // namespace

QString ffmpegMessage(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

Error ffmpegError(int code, const char* call) {
    // FFmpeg 的英文訊息只寫進日誌；Error 的訊息是給使用者看的
    qCInfo(lcFfmpeg).noquote() << call << "失敗：" << ffmpegMessage(code);
    return Error{ffmpegReason(code), code, QString::fromLatin1(call)};
}

Result<std::unique_ptr<IAudioSource>> FfmpegSource::open(const QString& path) {
    auto source = std::make_unique<FfmpegSource>();
    AVFormatContext* raw = nullptr;
    // FFmpeg 在 Windows 把 UTF-8 路徑轉成寬字元開檔（唯讀、共用讀寫），所以 Unicode 路徑可以直接用
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", kProtocolWhitelist, 0);
    av_dict_set(&options, "format_whitelist", kFormatWhitelist, 0);
    const QByteArray utf8Path = path.toUtf8();
    int rc = avformat_open_input(&raw, utf8Path.constData(), nullptr, &options);
    av_dict_free(&options);
    if (rc < 0) {
        Error error = ffmpegError(rc, "avformat_open_input");
        // 副檔名和內容不符（例：Wave64 存成 .wav）：內容是白名單以外的格式時說清楚
        const QString suffix = QFileInfo(path).suffix();
        const QString foreign =
            rc == AVERROR(EINVAL) && !suffix.isEmpty() ? foreignFormatName(utf8Path) : QString();
        if (!foreign.isEmpty()) {
            error.message = QStringLiteral("檔案內容不是 .%1 格式，可能是 %2").arg(suffix, foreign);
        }
        return error;
    }
    source->m_formatContext.reset(raw);
    if ((rc = avformat_find_stream_info(raw, nullptr)) < 0) {
        return ffmpegError(rc, "avformat_find_stream_info");
    }
    const AVCodec* decoder = nullptr;
    rc = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (rc < 0) {
        return ffmpegError(rc, "av_find_best_stream");
    }
    source->m_stream = rc;
    const AVStream* stream = raw->streams[rc];
    source->m_codec.reset(avcodec_alloc_context3(decoder));
    if (!source->m_codec || !source->m_packet || !source->m_frame) {
        return ffmpegError(AVERROR(ENOMEM), "av_alloc");
    }
    if ((rc = avcodec_parameters_to_context(source->m_codec.get(), stream->codecpar)) < 0) {
        return ffmpegError(rc, "avcodec_parameters_to_context");
    }
    source->m_codec->pkt_timebase = stream->time_base;
    if ((rc = avcodec_open2(source->m_codec.get(), decoder, nullptr)) < 0) {
        return ffmpegError(rc, "avcodec_open2");
    }

    // 先解出第一個框：樣本格式有些解碼器要到這時才知道，也順便確認檔案真的解得開
    const auto first = source->decodeNext();
    if (!first) {
        return first.error();
    }
    if (!first.value()) {
        return Error{QStringLiteral("檔案沒有可以解碼的音訊"), 0,
                     QStringLiteral("avcodec_receive_frame")};
    }
    if (!describe(*source->m_frame, *source->m_codec, *stream->codecpar, source->m_format)) {
        return Error{QStringLiteral("不支援的樣本格式：%1")
                         .arg(QString::fromLatin1(av_get_sample_fmt_name(
                             static_cast<AVSampleFormat>(source->m_frame->format)))),
                     0, QStringLiteral("describe")};
    }
    source->m_frameOffset = 0;
    if (stream->duration != AV_NOPTS_VALUE) {
        source->m_length = av_rescale_q(stream->duration, stream->time_base,
                                        AVRational{1, static_cast<int>(source->m_format.rate)});
    } else if (raw->duration != AV_NOPTS_VALUE) {
        source->m_length = av_rescale(raw->duration, source->m_format.rate, AV_TIME_BASE);
    }
    return std::unique_ptr<IAudioSource>(std::move(source));
}

QString FfmpegSourceFactory::name() const {
    return QStringLiteral("FFmpeg");
}

QStringList FfmpegSourceFactory::extensions() const {
    return {QStringLiteral("flac"), QStringLiteral("wav"),  QStringLiteral("aif"),
            QStringLiteral("aiff"), QStringLiteral("aifc"), QStringLiteral("ape"),
            QStringLiteral("wv"),   QStringLiteral("mp3"),  QStringLiteral("aac"),
            QStringLiteral("m4a"),  QStringLiteral("mp4"),  QStringLiteral("opus"),
            QStringLiteral("ogg"),  QStringLiteral("oga")};
}

Result<std::unique_ptr<IAudioSource>> FfmpegSourceFactory::open(const QString& path) {
    if (hasUrlScheme(path)) {
        qCInfo(lcFfmpeg).noquote() << "拒絕帶協定前綴的路徑：" << path;
        return Error{QStringLiteral("只開啟本機檔案，不開啟網址或協定路徑"), 0,
                     QStringLiteral("FfmpegSourceFactory::open")};
    }
    auto source = FfmpegSource::open(path);
    if (!source) {
        qCInfo(lcFfmpeg).noquote()
            << "無法開啟音源：" << path << source.error().call << source.error().message;
    }
    return source;
}

} // namespace auralis::core
