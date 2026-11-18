#pragma once

#include <auralis/audio_source.h>

#include <mutex>
#include <vector>

namespace auralis::core {

/// SourceRegistry 的實作。核心的工廠（FFmpeg）放在最後，任何模組的工廠都比它優先。
class SourceRegistryImpl final : public SourceRegistry {
public:
    /// 建立時就放入核心的 FFmpeg 工廠。
    SourceRegistryImpl();
    /// 只給測試用：指定核心的工廠（可以是 nullptr，表示沒有）。
    explicit SourceRegistryImpl(std::shared_ptr<ISourceFactory> coreFactory);

    void add(std::shared_ptr<ISourceFactory> factory) override;
    bool supports(const QString& path) const override;
    Result<std::unique_ptr<IAudioSource>> open(const QString& path) const override;
    Result<std::unique_ptr<IAudioSource>> openTrack(const TrackRef& track) const override;

private:
    std::shared_ptr<ISourceFactory> find(const QString& path) const;

    mutable std::mutex m_mutex;
    /// 模組註冊的工廠，後註冊的在後面（找的時候從後面往前）。
    std::vector<std::shared_ptr<ISourceFactory>> m_modules;
    std::shared_ptr<ISourceFactory> m_core;
};

} // namespace auralis::core
