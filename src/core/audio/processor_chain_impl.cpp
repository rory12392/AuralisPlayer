#include "audio/processor_chain_impl.h"

namespace auralis::core {

void ProcessorChainImpl::add(std::shared_ptr<IProcessorFactory> factory) {
    if (!factory) {
        return;
    }
    std::lock_guard lock(m_mutex);
    m_factories.push_back(std::move(factory));
}

std::vector<std::unique_ptr<IProcessor>> ProcessorChainImpl::createAll() const {
    std::vector<std::shared_ptr<IProcessorFactory>> factories;
    {
        std::lock_guard lock(m_mutex);
        factories = m_factories;
    }
    std::vector<std::unique_ptr<IProcessor>> processors;
    for (const auto& factory : factories) {
        if (auto processor = factory->create()) {
            processors.push_back(std::move(processor));
        }
    }
    return processors;
}

} // namespace auralis::core
