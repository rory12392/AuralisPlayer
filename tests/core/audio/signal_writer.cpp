#include "signal_writer.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

namespace auralis::test {

namespace {

QString message(int code, const char* call) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QStringLiteral("%1：%2").arg(QLatin1String(call), QString::fromUtf8(buffer));
}

struct OutputCloser {
    void operator()(AVFormatContext* p) const {
        if (p->pb) {
            avio_closep(&p->pb);
        }
        avformat_free_context(p);
    }
};
struct CodecFreer {
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
};
struct FrameFreer {
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};
struct PacketFreer {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};

/// 依格式挑編碼器支援的樣本格式（交錯或平面都可以，內容相同）。
AVSampleFormat pickSampleFormat(const AVCodecContext* context, const AVCodec* codec,
                                const AudioFormat& format) {
    AVSampleFormat packed = AV_SAMPLE_FMT_S16;
    if (format.isFloat) {
        packed = AV_SAMPLE_FMT_FLT;
    } else if (format.container == 32) {
        packed = AV_SAMPLE_FMT_S32;
    }
    const void* configs = nullptr;
    int count = 0;
    avcodec_get_supported_config(context, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs,
                                 &count);
    const auto* formats = static_cast<const AVSampleFormat*>(configs);
    if (formats == nullptr) {
        return packed;
    }
    for (int i = 0; i < count; ++i) {
        if (av_get_packed_sample_fmt(formats[i]) == packed) {
            return formats[i];
        }
    }
    return AV_SAMPLE_FMT_NONE;
}

/// 把交錯的 pcm 第 from 個樣本框起 count 個放進 frame（依 frame 的樣本格式交錯或分平面）。
void fillFrame(AVFrame* frame, const QByteArray& pcm, const AudioFormat& format, int from,
               int count) {
    const int sampleBytes = format.container / 8;
    const int channels = format.channels;
    const auto* source = reinterpret_cast<const uint8_t*>(pcm.constData());
    if (!av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->format))) {
        std::memcpy(frame->data[0], source + static_cast<size_t>(from) * channels * sampleBytes,
                    static_cast<size_t>(count) * channels * sampleBytes);
        return;
    }
    for (int i = 0; i < count; ++i) {
        for (int c = 0; c < channels; ++c) {
            std::memcpy(frame->extended_data[c] + static_cast<size_t>(i) * sampleBytes,
                        source + (static_cast<size_t>(from + i) * channels + c) * sampleBytes,
                        sampleBytes);
        }
    }
}

QString drain(AVCodecContext* codec, AVFormatContext* output, AVStream* stream, AVPacket* packet) {
    for (;;) {
        const int rc = avcodec_receive_packet(codec, packet);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            return {};
        }
        if (rc < 0) {
            return message(rc, "avcodec_receive_packet");
        }
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const int written = av_interleaved_write_frame(output, packet);
        if (written < 0) {
            return message(written, "av_interleaved_write_frame");
        }
    }
}

} // namespace

QByteArray makeNoise(const AudioFormat& format, int frames, quint32 seed) {
    std::mt19937 random(seed);
    const int samples = frames * format.channels;
    QByteArray pcm(samples * (format.container / 8), Qt::Uninitialized);
    for (int i = 0; i < samples; ++i) {
        char* out = pcm.data() + static_cast<qsizetype>(i) * (format.container / 8);
        if (format.isFloat) {
            const float value = std::uniform_real_distribution<float>(-1.0f, 1.0f)(random);
            std::memcpy(out, &value, sizeof(value));
        } else if (format.container == 16) {
            const auto value = static_cast<int16_t>(random());
            std::memcpy(out, &value, sizeof(value));
        } else {
            // 有效位元以下補 0（左對齊），和解碼器交出的排列相同
            const auto value = static_cast<int32_t>(random() & (0xFFFFFFFFu << (32 - format.bits)));
            std::memcpy(out, &value, sizeof(value));
        }
    }
    return pcm;
}

QString encodeFile(const QString& path, const char* codecName, const char* muxer,
                   const AudioFormat& format, const QByteArray& pcm) {
    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (codec == nullptr) {
        return QStringLiteral("找不到編碼器 %1").arg(QLatin1String(codecName));
    }
    std::unique_ptr<AVCodecContext, CodecFreer> context(avcodec_alloc_context3(codec));
    context->sample_rate = static_cast<int>(format.rate);
    av_channel_layout_default(&context->ch_layout, format.channels);
    context->sample_fmt = pickSampleFormat(context.get(), codec, format);
    if (context->sample_fmt == AV_SAMPLE_FMT_NONE) {
        return QStringLiteral("編碼器 %1 不支援這個樣本格式").arg(QLatin1String(codecName));
    }
    if (!format.isFloat) {
        context->bits_per_raw_sample = format.bits;
    }
    context->time_base = AVRational{1, static_cast<int>(format.rate)};
    // FFmpeg 內建的 Opus、Vorbis 編碼器標為實驗性
    context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    AVFormatContext* rawOutput = nullptr;
    const QByteArray utf8Path = path.toUtf8();
    int rc = avformat_alloc_output_context2(&rawOutput, nullptr, muxer, utf8Path.constData());
    if (rc < 0) {
        return message(rc, "avformat_alloc_output_context2");
    }
    std::unique_ptr<AVFormatContext, OutputCloser> output(rawOutput);
    if (output->oformat->flags & AVFMT_GLOBALHEADER) {
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    if ((rc = avcodec_open2(context.get(), codec, nullptr)) < 0) {
        return message(rc, "avcodec_open2");
    }
    AVStream* stream = avformat_new_stream(output.get(), nullptr);
    stream->time_base = context->time_base;
    if ((rc = avcodec_parameters_from_context(stream->codecpar, context.get())) < 0) {
        return message(rc, "avcodec_parameters_from_context");
    }
    if ((rc = avio_open(&output->pb, utf8Path.constData(), AVIO_FLAG_WRITE)) < 0) {
        return message(rc, "avio_open");
    }
    if ((rc = avformat_write_header(output.get(), nullptr)) < 0) {
        return message(rc, "avformat_write_header");
    }

    const int totalFrames = static_cast<int>(pcm.size() / format.bytesPerFrame());
    const bool variable = (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;
    const int frameSize = context->frame_size > 0 && !variable ? context->frame_size : 4096;
    std::unique_ptr<AVFrame, FrameFreer> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketFreer> packet(av_packet_alloc());
    for (int from = 0; from < totalFrames; from += frameSize) {
        const int count = std::min(frameSize, totalFrames - from);
        av_frame_unref(frame.get());
        frame->format = context->sample_fmt;
        frame->sample_rate = context->sample_rate;
        av_channel_layout_copy(&frame->ch_layout, &context->ch_layout);
        // 固定框長的編碼器不接受較短的最後一框時補零
        const bool pad = count < frameSize && context->frame_size > 0 && !variable &&
                         !(codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME);
        frame->nb_samples = pad ? frameSize : count;
        if ((rc = av_frame_get_buffer(frame.get(), 0)) < 0) {
            return message(rc, "av_frame_get_buffer");
        }
        if (pad) {
            av_samples_set_silence(frame->extended_data, 0, frameSize, format.channels,
                                   context->sample_fmt);
        }
        fillFrame(frame.get(), pcm, format, from, count);
        frame->pts = from;
        if ((rc = avcodec_send_frame(context.get(), frame.get())) < 0) {
            return message(rc, "avcodec_send_frame");
        }
        if (const QString error = drain(context.get(), output.get(), stream, packet.get());
            !error.isEmpty()) {
            return error;
        }
    }
    avcodec_send_frame(context.get(), nullptr);
    if (const QString error = drain(context.get(), output.get(), stream, packet.get());
        !error.isEmpty()) {
        return error;
    }
    if ((rc = av_write_trailer(output.get())) < 0) {
        return message(rc, "av_write_trailer");
    }
    return {};
}

} // namespace auralis::test
