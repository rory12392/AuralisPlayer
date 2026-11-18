#pragma once

#include <auralis/audio_output.h>

#include <QList>

#include <memory>
#include <mutex>

namespace auralis::core {

/// OutputRegistry 的實作。建構時先放核心的 WASAPI 獨占與共享（除非 withWasapi 為 false），
/// 模組的後端接在後面。查詢可以在任何非即時執行緒呼叫。
class OutputRegistryImpl final : public OutputRegistry {
public:
    explicit OutputRegistryImpl(bool withWasapi = true);

    void add(std::shared_ptr<IOutputBackendFactory> factory) override;
    void setDsdSink(std::shared_ptr<DsdSink> sink) override;

    /// 依註冊順序的全部輸出路徑。
    QList<std::shared_ptr<IOutputBackendFactory>> factories() const;
    /// 依 name() 找；沒有時 nullptr。
    std::shared_ptr<IOutputBackendFactory> find(const QString& name) const;
    /// 目前的 DSD 包裝器（播放前交給 PlaybackEngine::setDsdSink）；沒有時 nullptr。
    std::shared_ptr<DsdSink> dsdSink() const;

private:
    mutable std::mutex m_mutex;
    QList<std::shared_ptr<IOutputBackendFactory>> m_factories;
    std::shared_ptr<DsdSink> m_dsdSink;
};

} // namespace auralis::core
