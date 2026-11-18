#pragma once

// FFmpeg 音源的內部宣告：只給 ffmpeg_source.cpp（開檔與白名單）與
// ffmpeg_source_decode.cpp（解碼與跳轉）用，其他地方用 ffmpeg_source.h 的工廠。
#include <auralis/audio_source.h>

#include <QLoggingCategory>

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

Q_DECLARE_LOGGING_CATEGORY(lcFfmpeg)

namespace auralis::core {

QString ffmpegMessage(int code);
Error ffmpegError(int code, const char* call);

struct FormatCloser {
    void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
};
struct CodecFreer {
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
};
struct PacketFreer {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct FrameFreer {
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};

class FfmpegSource final : public IAudioSource {
public:
    static Result<std::unique_ptr<IAudioSource>> open(const QString& path);

    AudioFormat format() const override { return m_format; }
    qint64 lengthFrames() const override { return m_length; }
    Result<qint64> read(std::byte* dest, qint64 maxFrames) override;
    Result<qint64> seek(qint64 frame) override;

private:
    /// 取得下一個解碼好的框放進 m_frame；回傳 false 表示到結尾。
    Result<bool> decodeNext();
    qint64 frameStart() const;

    std::unique_ptr<AVFormatContext, FormatCloser> m_formatContext;
    std::unique_ptr<AVCodecContext, CodecFreer> m_codec;
    std::unique_ptr<AVPacket, PacketFreer> m_packet{av_packet_alloc()};
    std::unique_ptr<AVFrame, FrameFreer> m_frame{av_frame_alloc()};
    int m_stream = -1;
    AudioFormat m_format;
    qint64 m_length = -1;
    /// m_frame 裡還沒交出去的樣本從哪裡開始；-1 表示 m_frame 沒有資料。
    int m_frameOffset = -1;
    bool m_flushed = false;
    qint64 m_position = 0;
    /// seek 之後要丟到哪個樣本框為止；-1 表示不用丟。
    qint64 m_seekTarget = -1;
};

} // namespace auralis::core
