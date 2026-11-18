// FFmpeg 音源：解碼、交出樣本、跳轉。開檔與白名單在 ffmpeg_source.cpp。
#include "audio/ffmpeg_source_impl.h"

#include <algorithm>
#include <cstring>

namespace auralis::core {

namespace {

/// 連續這麼多個封包解不開就當成檔案損毀（偶爾一個壞封包時跳過，有損格式常見）。
constexpr int kMaxConsecutiveErrors = 16;

/// 把 frame 的第 from 個樣本起、count 個樣本框交錯寫進 dest。整數直接搬，位元不變。
void copyFrames(const AVFrame& frame, int from, int count, std::byte* dest,
                const AudioFormat& format) {
    const auto sampleFormat = static_cast<AVSampleFormat>(frame.format);
    const int channels = format.channels;
    const int outBytes = format.container / 8;
    if (sampleFormat == AV_SAMPLE_FMT_S16 || sampleFormat == AV_SAMPLE_FMT_S32 ||
        sampleFormat == AV_SAMPLE_FMT_FLT) {
        const size_t frameBytes = static_cast<size_t>(channels) * outBytes;
        std::memcpy(dest, frame.data[0] + static_cast<size_t>(from) * frameBytes,
                    static_cast<size_t>(count) * frameBytes);
        return;
    }
    const bool planar = av_sample_fmt_is_planar(sampleFormat) != 0;
    for (int i = 0; i < count; ++i) {
        for (int c = 0; c < channels; ++c) {
            const int index = planar ? from + i : (from + i) * channels + c;
            const uint8_t* plane = frame.extended_data[planar ? c : 0];
            std::byte* out = dest + (static_cast<size_t>(i) * channels + c) * outBytes;
            switch (sampleFormat) {
            case AV_SAMPLE_FMT_U8:
            case AV_SAMPLE_FMT_U8P: {
                const auto value = static_cast<int16_t>((plane[index] - 128) * 256);
                std::memcpy(out, &value, sizeof(value));
                break;
            }
            case AV_SAMPLE_FMT_DBL:
            case AV_SAMPLE_FMT_DBLP: {
                const auto value =
                    static_cast<float>(reinterpret_cast<const double*>(plane)[index]);
                std::memcpy(out, &value, sizeof(value));
                break;
            }
            default: // S16P、S32P、FLTP：容器大小與輸出相同
                std::memcpy(out, plane + static_cast<size_t>(index) * outBytes, outBytes);
                break;
            }
        }
    }
}

} // namespace

Result<bool> FfmpegSource::decodeNext() {
    int errors = 0;
    const auto failed = [&](int rc, const char* call) -> bool {
        qCDebug(lcFfmpeg).noquote() << call << ffmpegMessage(rc);
        return ++errors > kMaxConsecutiveErrors;
    };
    for (;;) {
        int rc = avcodec_receive_frame(m_codec.get(), m_frame.get());
        if (rc == 0) {
            return true;
        }
        if (rc == AVERROR_EOF) {
            return false;
        }
        if (rc != AVERROR(EAGAIN)) {
            if (failed(rc, "avcodec_receive_frame")) {
                return ffmpegError(rc, "avcodec_receive_frame");
            }
            continue;
        }
        if (m_flushed) {
            return false;
        }
        rc = av_read_frame(m_formatContext.get(), m_packet.get());
        if (rc == AVERROR_EOF) {
            avcodec_send_packet(m_codec.get(), nullptr);
            m_flushed = true;
            continue;
        }
        if (rc < 0) {
            if (rc != AVERROR_INVALIDDATA || failed(rc, "av_read_frame")) {
                return ffmpegError(rc, "av_read_frame");
            }
            continue;
        }
        if (m_packet->stream_index != m_stream) {
            av_packet_unref(m_packet.get());
            continue;
        }
        rc = avcodec_send_packet(m_codec.get(), m_packet.get());
        av_packet_unref(m_packet.get());
        if (rc < 0 && rc != AVERROR(EAGAIN) && failed(rc, "avcodec_send_packet")) {
            return ffmpegError(rc, "avcodec_send_packet");
        }
    }
}

qint64 FfmpegSource::frameStart() const {
    const AVStream* stream = m_formatContext->streams[m_stream];
    const int64_t pts = m_frame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) {
        return m_position;
    }
    const int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    return av_rescale_q(pts - start, stream->time_base,
                        AVRational{1, static_cast<int>(m_format.rate)});
}

Result<qint64> FfmpegSource::read(std::byte* dest, qint64 maxFrames) {
    qint64 written = 0;
    const int frameBytes = m_format.bytesPerFrame();
    while (written < maxFrames) {
        if (m_frameOffset < 0) {
            const auto next = decodeNext();
            if (!next) {
                return next.error();
            }
            if (!next.value()) {
                break;
            }
            if (m_frame->sample_rate != static_cast<int>(m_format.rate) ||
                m_frame->ch_layout.nb_channels != m_format.channels) {
                return Error{QStringLiteral("串流中途改變了取樣率或聲道數"), 0,
                             QStringLiteral("read")};
            }
            m_frameOffset = 0;
            if (m_seekTarget >= 0) {
                // seek 落在目標之前的關鍵點：丟掉目標之前的樣本，讓結果和從頭解碼相同
                const qint64 start = frameStart();
                if (start + m_frame->nb_samples <= m_seekTarget) {
                    m_frameOffset = -1;
                    continue;
                }
                m_frameOffset = static_cast<int>(std::max<qint64>(0, m_seekTarget - start));
                m_seekTarget = -1;
            }
        }
        const int available = m_frame->nb_samples - m_frameOffset;
        const int count = static_cast<int>(std::min<qint64>(available, maxFrames - written));
        copyFrames(*m_frame, m_frameOffset, count, dest + written * frameBytes, m_format);
        written += count;
        m_frameOffset += count;
        if (m_frameOffset >= m_frame->nb_samples) {
            m_frameOffset = -1;
        }
    }
    m_position += written;
    return written;
}

Result<qint64> FfmpegSource::seek(qint64 frame) {
    if (frame < 0) {
        return Error{QStringLiteral("位置不能是負的"), 0, QStringLiteral("seek")};
    }
    const AVStream* stream = m_formatContext->streams[m_stream];
    const int64_t start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    const int64_t ts =
        av_rescale_q(frame, AVRational{1, static_cast<int>(m_format.rate)}, stream->time_base) +
        start;
    const int rc = avformat_seek_file(m_formatContext.get(), m_stream, INT64_MIN, ts, ts, 0);
    if (rc < 0) {
        return ffmpegError(rc, "avformat_seek_file");
    }
    avcodec_flush_buffers(m_codec.get());
    m_frameOffset = -1;
    m_flushed = false;
    m_seekTarget = frame;
    m_position = frame;
    return frame;
}

} // namespace auralis::core
