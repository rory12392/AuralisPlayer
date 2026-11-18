#include "dsd_source.h"

#include "dsd_stream.h"

#include <algorithm>
#include <array>
#include <vector>

namespace auralis::mod::out {

namespace {

/// DFF 一次從檔案讀幾個樣本框。
constexpr qint64 kDffChunkFrames = 4096;
/// 一個樣本框裡每個聲道的位元組數。
constexpr qint64 kFrameBytes = 4;

/// 位元組的位元順序反轉表（DSF 的每樣本 1 位元是最低位在前）。
constexpr std::array<quint8, 256> makeReverseTable() {
    std::array<quint8, 256> table{};
    for (unsigned value = 0; value < 256; ++value) {
        unsigned reversed = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            reversed |= ((value >> bit) & 1u) << (7 - bit);
        }
        table[value] = static_cast<quint8>(reversed);
    }
    return table;
}
constexpr std::array<quint8, 256> kReverse = makeReverseTable();

Error readError() {
    return Error{QStringLiteral("讀取檔案時發生錯誤"), 0, QStringLiteral("DsdFile::readAt")};
}

class DsdSource final : public IAudioSource {
public:
    enum class Layout { DsfBlocks, DffInterleaved };

    DsdSource(std::unique_ptr<DsdFile> file, const DsdStreamInfo& info, Layout layout)
        : m_file(std::move(file)), m_info(info), m_layout(layout),
          m_scratch(static_cast<std::size_t>(
              info.channels *
              (layout == Layout::DsfBlocks ? kDsfBlockBytes : kDffChunkFrames * kFrameBytes))) {}

    AudioFormat format() const override {
        AudioFormat format;
        format.dsdRate = m_info.dsdRate;
        format.bits = 1;
        format.container = 32;
        format.channels = m_info.channels;
        return format;
    }

    qint64 lengthFrames() const override { return m_info.frames; }

    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const qint64 want = std::min(maxFrames, m_info.frames - m_frame);
        qint64 done = 0;
        while (done < want) {
            auto* out = reinterpret_cast<quint8*>(dest) + done * kFrameBytes * m_info.channels;
            const qint64 frames = m_layout == Layout::DsfBlocks ? readDsf(out, want - done)
                                                                : readDff(out, want - done);
            if (frames == 0) {
                return readError();
            }
            done += frames;
            m_frame += frames;
        }
        return done;
    }

    Result<qint64> seek(qint64 frame) override {
        m_frame = std::clamp<qint64>(frame, 0, m_info.frames);
        return m_frame;
    }

private:
    /// DSF：每個聲道一塊 4096 位元組、各聲道的塊輪流排。一次處理目前這一組塊裡剩下的樣本框。
    qint64 readDsf(quint8* out, qint64 maxFrames) noexcept {
        const qint64 framesPerBlock = kDsfBlockBytes / kFrameBytes;
        const qint64 group = m_frame / framesPerBlock;
        const int channels = m_info.channels;
        if (group != m_loadedGroup) {
            if (!m_file->readAt(m_info.dataOffset + group * channels * kDsfBlockBytes,
                                m_scratch.data(), channels * kDsfBlockBytes)) {
                return 0;
            }
            m_loadedGroup = group;
        }
        const qint64 within = m_frame % framesPerBlock;
        const qint64 frames = std::min(maxFrames, framesPerBlock - within);
        for (int ch = 0; ch < channels; ++ch) {
            const quint8* in = m_scratch.data() + ch * kDsfBlockBytes + within * kFrameBytes;
            quint8* o = out + ch * kFrameBytes;
            for (qint64 i = 0; i < frames * kFrameBytes; i += kFrameBytes) {
                for (qint64 k = 0; k < kFrameBytes; ++k) {
                    o[k] = m_info.lsbFirst ? kReverse[in[i + k]] : in[i + k];
                }
                o += kFrameBytes * channels;
            }
        }
        return frames;
    }

    /// DFF：各聲道以位元組為單位輪流排（最高位在前），重排成每聲道 4 位元組一組。
    qint64 readDff(quint8* out, qint64 maxFrames) noexcept {
        const int channels = m_info.channels;
        const qint64 frames = std::min(maxFrames, kDffChunkFrames);
        const qint64 frameBytes = kFrameBytes * channels;
        if (!m_file->readAt(m_info.dataOffset + m_frame * frameBytes, m_scratch.data(),
                            frames * frameBytes)) {
            return 0;
        }
        const quint8* in = m_scratch.data();
        for (qint64 f = 0; f < frames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                for (qint64 k = 0; k < kFrameBytes; ++k) {
                    out[ch * kFrameBytes + k] = in[k * channels + ch];
                }
            }
            in += frameBytes;
            out += frameBytes;
        }
        return frames;
    }

    std::unique_ptr<DsdFile> m_file;
    DsdStreamInfo m_info;
    Layout m_layout;
    std::vector<quint8> m_scratch;
    qint64 m_frame = 0;
    /// m_scratch 裡目前是 DSF 的第幾組塊；-1 表示還沒讀。
    qint64 m_loadedGroup = -1;
};

} // namespace

QString DsdSourceFactory::name() const {
    return QStringLiteral("DSD（DSF／DFF）");
}

QStringList DsdSourceFactory::extensions() const {
    return {QStringLiteral("dsf"), QStringLiteral("dff")};
}

Result<std::unique_ptr<IAudioSource>> DsdSourceFactory::open(const QString& path) {
    auto file = DsdFile::open(path);
    if (!file) {
        return file.error();
    }
    const bool dff = path.endsWith(QStringLiteral(".dff"), Qt::CaseInsensitive);
    auto info = dff ? parseDffHeader(*file.value()) : parseDsfHeader(*file.value());
    if (!info) {
        return info.error();
    }
    return std::unique_ptr<IAudioSource>(std::make_unique<DsdSource>(
        std::move(file).value(), info.value(),
        dff ? DsdSource::Layout::DffInterleaved : DsdSource::Layout::DsfBlocks));
}

} // namespace auralis::mod::out
