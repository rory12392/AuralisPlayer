#pragma once

#include <auralis/audio_processor.h>

#include <mutex>
#include <vector>

namespace auralis::core {

/// ProcessorChain 的實作：保存模組註冊的工廠，播放每首曲目時建立一組新的處理器。
class ProcessorChainImpl final : public ProcessorChain {
public:
    ProcessorChainImpl() = default;

    void add(std::shared_ptr<IProcessorFactory> factory) override;
    /// 依註冊順序各建立一個處理器（解碼執行緒；工廠回傳 nullptr 的略過）。
    std::vector<std::unique_ptr<IProcessor>> createAll() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<IProcessorFactory>> m_factories;
};

} // namespace auralis::core
