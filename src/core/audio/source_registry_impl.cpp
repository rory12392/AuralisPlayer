#include "audio/source_registry_impl.h"

#include "audio/ffmpeg_source.h"
#include "library/cue_tracks.h"

#include <QFileInfo>

#include <algorithm>

namespace auralis::core {

namespace {

bool accepts(const ISourceFactory& factory, const QString& suffix) {
    return factory.extensions().contains(suffix);
}

/// 由進階輸出模組（OUT）提供音源的副檔名（DSF、DFF）。
bool isModuleDsdSuffix(const QString& suffix) {
    return suffix.compare(QLatin1String("dsf"), Qt::CaseInsensitive) == 0 ||
           suffix.compare(QLatin1String("dff"), Qt::CaseInsensitive) == 0;
}

/// CUE 區段：整軌檔的音源切成一軌的範圍，位置從這一軌的起點算（給依曲目身分開音源的模組；
/// 播放引擎自己依區段讀，不用這個）。
class SegmentSource final : public IAudioSource {
public:
    SegmentSource(std::unique_ptr<IAudioSource> inner, qint64 start, qint64 end)
        : m_inner(std::move(inner)), m_start(start), m_end(end) {}

    AudioFormat format() const override { return m_inner->format(); }

    qint64 lengthFrames() const override {
        if (m_end >= 0) {
            return m_end - m_start;
        }
        const qint64 total = m_inner->lengthFrames();
        return total < 0 ? -1 : std::max<qint64>(0, total - m_start);
    }

    Result<qint64> read(std::byte* dest, qint64 maxFrames) override {
        const qint64 want =
            m_end >= 0 ? std::min(maxFrames, m_end - m_start - m_position) : maxFrames;
        if (want <= 0) {
            return qint64{0};
        }
        auto read = m_inner->read(dest, want);
        if (read) {
            m_position += read.value();
        }
        return read;
    }

    Result<qint64> seek(qint64 frame) override {
        if (frame < 0) {
            return Error{QStringLiteral("位置不能是負的"), 0, QStringLiteral("seek")};
        }
        auto sought = m_inner->seek(m_start + frame);
        if (!sought) {
            return sought.error();
        }
        m_position = frame;
        return frame;
    }

private:
    std::unique_ptr<IAudioSource> m_inner;
    qint64 m_start = 0;
    qint64 m_end = -1;
    qint64 m_position = 0; ///< 從這一軌的起點算
};

} // namespace

SourceRegistryImpl::SourceRegistryImpl()
    : SourceRegistryImpl(std::make_shared<FfmpegSourceFactory>()) {}

SourceRegistryImpl::SourceRegistryImpl(std::shared_ptr<ISourceFactory> coreFactory)
    : m_core(std::move(coreFactory)) {}

void SourceRegistryImpl::add(std::shared_ptr<ISourceFactory> factory) {
    if (!factory) {
        return;
    }
    const std::lock_guard lock(m_mutex);
    m_modules.push_back(std::move(factory));
}

std::shared_ptr<ISourceFactory> SourceRegistryImpl::find(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty()) {
        return nullptr;
    }
    const std::lock_guard lock(m_mutex);
    for (auto it = m_modules.rbegin(); it != m_modules.rend(); ++it) {
        if (accepts(**it, suffix)) {
            return *it;
        }
    }
    if (m_core && accepts(*m_core, suffix)) {
        return m_core;
    }
    return nullptr;
}

bool SourceRegistryImpl::supports(const QString& path) const {
    return find(path) != nullptr;
}

Result<std::unique_ptr<IAudioSource>> SourceRegistryImpl::open(const QString& path) const {
    // 工廠在鎖外開檔：開檔可能很久，不能擋住其他執行緒的 supports()
    const auto factory = find(path);
    if (!factory) {
        const QString suffix = QFileInfo(path).suffix();
        // DSD 檔的音源在進階輸出模組：模組停用（或啟動失敗）時沒有人收，
        // 照模組規格 §1 的停用方式說明原因，不寫成不支援的格式
        if (isModuleDsdSuffix(suffix)) {
            return Error{QStringLiteral("進階輸出已停用"), 0,
                         QStringLiteral("SourceRegistry::open")};
        }
        return Error{QStringLiteral("不支援的格式：%1").arg(suffix), 0,
                     QStringLiteral("SourceRegistry::open")};
    }
    return factory->open(path);
}

Result<std::unique_ptr<IAudioSource>> SourceRegistryImpl::openTrack(const TrackRef& track) const {
    // 和播放走同一個 openTrack：整檔與 CUE 區段的規則只有一份
    auto opened = core::openTrack(track, *this);
    if (!opened) {
        return opened.error();
    }
    EngineTrack& engineTrack = opened.value();
    if (engineTrack.startFrame == 0 && engineTrack.endFrame < 0) {
        return std::move(engineTrack.source);
    }
    auto segment = std::make_unique<SegmentSource>(std::move(engineTrack.source),
                                                   engineTrack.startFrame, engineTrack.endFrame);
    if (auto sought = segment->seek(0); !sought) {
        return sought.error();
    }
    return std::unique_ptr<IAudioSource>(std::move(segment));
}

} // namespace auralis::core
