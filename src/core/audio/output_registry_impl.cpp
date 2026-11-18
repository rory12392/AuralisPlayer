#include "audio/output_registry_impl.h"

#include "audio/wasapi_backend.h"

namespace auralis::core {

OutputRegistryImpl::OutputRegistryImpl(bool withWasapi) {
    if (withWasapi) {
        add(std::make_shared<WasapiBackendFactory>(WasapiMode::Exclusive));
        add(std::make_shared<WasapiBackendFactory>(WasapiMode::Shared));
    }
}

void OutputRegistryImpl::add(std::shared_ptr<IOutputBackendFactory> factory) {
    if (!factory) {
        return;
    }
    const QString name = factory->name();
    std::lock_guard lock(m_mutex);
    for (auto& existing : m_factories) {
        if (existing->name() == name) {
            existing = std::move(factory);
            return;
        }
    }
    m_factories.append(std::move(factory));
}

void OutputRegistryImpl::setDsdSink(std::shared_ptr<DsdSink> sink) {
    std::lock_guard lock(m_mutex);
    m_dsdSink = std::move(sink);
}

QList<std::shared_ptr<IOutputBackendFactory>> OutputRegistryImpl::factories() const {
    std::lock_guard lock(m_mutex);
    return m_factories;
}

std::shared_ptr<IOutputBackendFactory> OutputRegistryImpl::find(const QString& name) const {
    std::lock_guard lock(m_mutex);
    for (const auto& factory : m_factories) {
        if (factory->name() == name) {
            return factory;
        }
    }
    return nullptr;
}

std::shared_ptr<DsdSink> OutputRegistryImpl::dsdSink() const {
    std::lock_guard lock(m_mutex);
    return m_dsdSink;
}

} // namespace auralis::core
